#include "storage_service.h"
#include "board.h"
#include "app_config.h"
#include "photo_file.h"
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/semphr.h"
#include "sdmmc_cmd.h"
static const char *TAG = "storage";
static SemaphoreHandle_t s_lock, s_sd_lock;
static sdmmc_card_t *s_sd_card;
static bool s_sd_mounted, s_session_active, s_file_stream_active;
static storage_status_t s_status;
static uint32_t s_photo_sequence = 1;
static void update_sd_status(bool ready, uint64_t total_bytes, uint64_t free_bytes,
                             const char *error)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.sd_ready = ready;
    s_status.sd_total_bytes = total_bytes;
    s_status.sd_free_bytes = free_bytes;
    strlcpy(s_status.sd_error, error ? error : "", sizeof(s_status.sd_error));
    xSemaphoreGive(s_lock);
}

static void unmount_sd_card_locked(void)
{
    if (s_sd_mounted && s_sd_card) {
        esp_err_t result = esp_vfs_fat_sdcard_unmount(APP_SD_MOUNT_POINT, s_sd_card);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "SD card unmount failed: %s", esp_err_to_name(result));
        }
    }
    s_sd_card = NULL;
    s_sd_mounted = false;
    update_sd_status(false, 0, 0, "SD card is not mounted");
}

static bool is_safe_file_name(const char *name)
{
    if (!name || name[0] == '\0' || name[0] == '.') {
        return false;
    }
    size_t length = strlen(name);
    if (length >= sizeof(((storage_file_info_t *)0)->name) || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        if (character < 0x20 || character == '/' || character == '\\' || character == '"') {
            return false;
        }
    }
    return strstr(name, "..") == NULL;
}

static bool build_file_path(const char *name, char *path, size_t path_size)
{
    if (!is_safe_file_name(name) || !path || path_size == 0) {
        return false;
    }
    int length = snprintf(path, path_size, APP_SD_MOUNT_POINT "/%s", name);
    return length >= 0 && (size_t)length < path_size;
}

static bool is_temporary_file(const char *name)
{
    size_t length = strlen(name);
    return (length > 5 && strcmp(name + length - 5, ".part") == 0) ||
           (length > 9 && strcmp(name + length - 9, ".part.idx") == 0);
}

static bool parse_indexed_file_name(const char *name, const char *prefix, const char *suffix,
                                    uint32_t *sequence)
{
    if (!name || !prefix || !suffix || !sequence || strncmp(name, prefix, strlen(prefix)) != 0) {
        return false;
    }
    const char *digits = name + strlen(prefix);
    if (!isdigit((unsigned char)*digits)) {
        return false;
    }
    char *end = NULL;
    unsigned long value = strtoul(digits, &end, 10);
    if (end == digits || strcmp(end, suffix) != 0 || value > UINT32_MAX) {
        return false;
    }
    *sequence = (uint32_t)value;
    return true;
}

static uint32_t next_photo_sequence_locked(void)
{
    uint32_t next = s_photo_sequence;
    DIR *directory = opendir(APP_SD_MOUNT_POINT);
    if (directory) {
        struct dirent *entry;
        while ((entry = readdir(directory)) != NULL) {
            uint32_t sequence = 0;
            if (parse_indexed_file_name(entry->d_name, "photo_", ".jpg", &sequence) &&
                sequence < UINT32_MAX && sequence + 1 > next) {
                next = sequence + 1;
            }
        }
        closedir(directory);
    }
    return next;
}

static esp_err_t verify_mounted_sd_card(void)
{
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
    esp_err_t result = esp_vfs_fat_info(APP_SD_MOUNT_POINT, &total_bytes, &free_bytes);
    if (result != ESP_OK) {
        update_sd_status(false, 0, 0, "FAT filesystem capacity check failed");
        return result;
    }

    const char probe_path[] = APP_SD_MOUNT_POINT "/.mcu_sd_probe";
    FILE *probe = fopen(probe_path, "wb");
    static const char probe_data[] = "mcu-sd-check\n";
    bool probe_ok = probe && fwrite(probe_data, 1, sizeof(probe_data) - 1, probe) ==
                                       sizeof(probe_data) - 1;
    if (probe_ok && fflush(probe) != 0) {
        probe_ok = false;
    }
    if (probe_ok && fsync(fileno(probe)) != 0) {
        probe_ok = false;
    }
    if (probe && fclose(probe) != 0) {
        probe_ok = false;
    }
    if (remove(probe_path) != 0) {
        probe_ok = false;
    }
    if (!probe_ok) {
        update_sd_status(false, total_bytes, free_bytes, "SD card write test failed");
        return ESP_FAIL;
    }

    update_sd_status(true, total_bytes, free_bytes, "");
    ESP_LOGI(TAG, "SD card check passed: total=%" PRIu64 " MiB, free=%" PRIu64 " MiB",
             total_bytes / (1024 * 1024), free_bytes / (1024 * 1024));
    return ESP_OK;
}

static esp_err_t mount_sd_card(void)
{
    const board_sd_pins_t *pins = board_get_config()->sd;
    if (!pins) return ESP_ERR_NOT_SUPPORTED;
    if (s_sd_mounted && s_sd_card) {
        return ESP_OK;
    }

    s_sd_card = NULL;
    s_sd_mounted = false;
    update_sd_status(false, 0, 0, "SD card is not mounted");

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // The onboard 1-bit socket does not need DDR. Single-data-rate mode is
    // more tolerant of cards and board-level signal quality.
    host.flags &= ~SDMMC_HOST_FLAG_DDR;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = pins->clk;
    slot_config.cmd = pins->cmd;
    slot_config.d0 = pins->d0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    ESP_LOGI(TAG, "Mounting SDMMC 1-bit card: CLK=%d CMD=%d D0=%d", pins->clk,
             pins->cmd, pins->d0);
    esp_err_t result = esp_vfs_fat_sdmmc_mount(APP_SD_MOUNT_POINT, &host, &slot_config, &mount_config,
                                               &s_sd_card);
    if (result == ESP_OK) {
        s_sd_mounted = true;
        ESP_LOGI(TAG, "SD card mounted at %s", APP_SD_MOUNT_POINT);
        sdmmc_card_print_info(stdout, s_sd_card);

        result = verify_mounted_sd_card();
        if (result != ESP_OK) {
            char check_error[sizeof(s_status.sd_error)];
            xSemaphoreTake(s_lock, portMAX_DELAY);
            strlcpy(check_error, s_status.sd_error, sizeof(check_error));
            xSemaphoreGive(s_lock);
            unmount_sd_card_locked();
            update_sd_status(false, 0, 0, check_error[0] ? check_error : "SD card check failed");
        }
    } else {
        s_sd_card = NULL;
        update_sd_status(false, 0, 0, esp_err_to_name(result));
        ESP_LOGW(TAG, "SD card mount failed: %s", esp_err_to_name(result));
    }
    return result;
}

static void report_photo_error(const char *operation, int error)
{
    if (!error) error = EIO;
    ESP_LOGE(TAG, "Photo %s failed: %s (errno=%d)", operation, strerror(error), error);
    if (error == EIO || error == ENODEV || error == ENXIO) {
        unmount_sd_card_locked();
    } else {
        // Full or read-only media remains available for listing/downloads and,
        // where possible, deletion. Mount probing cannot recover a full card.
        uint64_t total = 0, free_bytes = 0;
        esp_vfs_fat_info(APP_SD_MOUNT_POINT, &total, &free_bytes);
        update_sd_status(s_sd_mounted, total, free_bytes, strerror(error));
    }
}

static esp_err_t save_photo_locked(const uint8_t *data, size_t length, char *path, size_t path_size)
{
    if (!data || length == 0 || !path || path_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = mount_sd_card();
    if (result != ESP_OK) {
        return result;
    }

    uint32_t sequence = next_photo_sequence_locked();
    char candidate[96];
    size_t candidate_size = path_size < sizeof(candidate) ? path_size : sizeof(candidate);
    FILE *file = photo_file_create(APP_SD_MOUNT_POINT, &sequence, candidate, candidate_size);
    if (!file) {
        int error = errno;
        if (error == EINVAL || error == ENAMETOOLONG) return ESP_ERR_INVALID_ARG;
        report_photo_error("creation", error);
        return ESP_FAIL;
    }
    if (sequence < UINT32_MAX) s_photo_sequence = sequence + 1;

    errno = 0;
    bool written = fwrite(data, 1, length, file) == length;
    int error = written ? 0 : (errno ? errno : EIO);
    if (written && fflush(file) != 0) {
        error = errno ? errno : EIO;
        written = false;
    }
    if (written && fsync(fileno(file)) != 0) {
        error = errno ? errno : EIO;
        written = false;
    }
    if (fclose(file) != 0 && written) {
        error = errno ? errno : EIO;
        written = false;
    }
    if (!written) {
        remove(candidate);
        report_photo_error("write", error);
        return ESP_FAIL;
    }

    strlcpy(path, candidate, path_size);
    return ESP_OK;
}

esp_err_t storage_service_save_photo(const uint8_t *data, size_t length, char *path, size_t path_size)
{
    if (!s_lock || !s_sd_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_sd_lock, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = save_photo_locked(data, length, path, path_size);
    xSemaphoreGive(s_sd_lock);
    return result;
}

esp_err_t storage_service_check_sd(void)
{
    if (storage_service_is_busy()) return ESP_ERR_INVALID_STATE;
    if (!s_lock || !s_sd_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_sd_lock, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool session_active = s_session_active;
    xSemaphoreGive(s_lock);
    if (session_active) {
        xSemaphoreGive(s_sd_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = s_sd_mounted && s_sd_card ? verify_mounted_sd_card() : mount_sd_card();
    if (result != ESP_OK && s_sd_mounted) {
        char check_error[sizeof(s_status.sd_error)];
        xSemaphoreTake(s_lock, portMAX_DELAY);
        strlcpy(check_error, s_status.sd_error, sizeof(check_error));
        xSemaphoreGive(s_lock);
        unmount_sd_card_locked();
        update_sd_status(false, 0, 0, check_error[0] ? check_error : "SD card check failed");
    }
    xSemaphoreGive(s_sd_lock);
    return result;
}

esp_err_t storage_service_list_files(storage_file_info_t *files, size_t capacity,
                                       size_t *count, bool *truncated)
{
    if (storage_service_is_busy()) return ESP_ERR_INVALID_STATE;
    if (!s_lock || !s_sd_lock || !files || capacity == 0 || !count) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = 0;
    if (truncated) {
        *truncated = false;
    }
    if (xSemaphoreTake(s_sd_lock, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool storage_busy = s_session_active || s_file_stream_active;
    xSemaphoreGive(s_lock);
    if (storage_busy) {
        xSemaphoreGive(s_sd_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = mount_sd_card();
    if (result == ESP_OK) {
        DIR *directory = opendir(APP_SD_MOUNT_POINT);
        if (!directory) {
            result = ESP_FAIL;
        } else {
            struct dirent *entry;
            while ((entry = readdir(directory)) != NULL) {
                char path[320];
                struct stat file_stat;
                if (!is_safe_file_name(entry->d_name) || is_temporary_file(entry->d_name) ||
                    !build_file_path(entry->d_name, path, sizeof(path)) ||
                    stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
                    continue;
                }
                if (*count >= capacity) {
                    if (truncated) {
                        *truncated = true;
                    }
                    continue;
                }
                strlcpy(files[*count].name, entry->d_name, sizeof(files[*count].name));
                files[*count].size_bytes = (uint64_t)file_stat.st_size;
                ++*count;
            }
            closedir(directory);
            uint64_t total_bytes = 0;
            uint64_t free_bytes = 0;
            if (esp_vfs_fat_info(APP_SD_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
                update_sd_status(true, total_bytes, free_bytes, "");
            }
        }
    }
    xSemaphoreGive(s_sd_lock);
    return result;
}

esp_err_t storage_service_delete_file(const char *name)
{
    if (storage_service_is_busy()) return ESP_ERR_INVALID_STATE;
    if (!s_lock || !s_sd_lock || !is_safe_file_name(name) || is_temporary_file(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_sd_lock, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool storage_busy = s_session_active || s_file_stream_active;
    xSemaphoreGive(s_lock);
    if (storage_busy) {
        xSemaphoreGive(s_sd_lock);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = mount_sd_card();
    char path[320];
    if (result == ESP_OK && build_file_path(name, path, sizeof(path))) {
        struct stat file_stat;
        if (stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            result = ESP_ERR_NOT_FOUND;
        } else if (remove(path) != 0) {
            result = ESP_FAIL;
        }
    } else if (result == ESP_OK) {
        result = ESP_ERR_INVALID_ARG;
    }
    if (result == ESP_OK) {
        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        if (esp_vfs_fat_info(APP_SD_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
            update_sd_status(true, total_bytes, free_bytes, "");
        }
        ESP_LOGI(TAG, "Deleted SD file: %s", name);
    }
    xSemaphoreGive(s_sd_lock);
    return result;
}

esp_err_t storage_service_stream_file(const char *name, storage_chunk_callback_t callback,
                                        void *context)
{
    if (storage_service_is_busy()) return ESP_ERR_INVALID_STATE;
    if (!s_lock || !s_sd_lock || !callback || !is_safe_file_name(name) ||
        is_temporary_file(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_sd_lock, pdMS_TO_TICKS(15000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_session_active || s_file_stream_active) {
        xSemaphoreGive(s_lock);
        xSemaphoreGive(s_sd_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_file_stream_active = true;
    xSemaphoreGive(s_lock);

    esp_err_t result = mount_sd_card();
    char path[320];
    FILE *file = NULL;
    if (result == ESP_OK && build_file_path(name, path, sizeof(path))) {
        struct stat file_stat;
        if (stat(path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            result = ESP_ERR_NOT_FOUND;
        } else {
            file = fopen(path, "rb");
            if (!file) {
                result = ESP_ERR_NOT_FOUND;
            }
        }
    } else if (result == ESP_OK) {
        result = ESP_ERR_INVALID_ARG;
    }
    if (result == ESP_OK) {
        uint8_t *buffer = malloc(4096);
        if (!buffer) {
            fclose(file);
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_file_stream_active = false;
            xSemaphoreGive(s_lock);
            xSemaphoreGive(s_sd_lock);
            return ESP_ERR_NO_MEM;
        }
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, 4096, file)) > 0) {
            esp_err_t callback_result = callback(buffer, bytes_read, context);
            if (callback_result != ESP_OK) {
                result = callback_result;
                break;
            }
        }
        if (ferror(file)) {
            result = ESP_FAIL;
        }
        free(buffer);
    }
    if (file && fclose(file) != 0) {
        result = ESP_FAIL;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_file_stream_active = false;
    xSemaphoreGive(s_lock);
    xSemaphoreGive(s_sd_lock);
    return result;
}

esp_err_t storage_service_init(void)
{
    if (s_lock && s_sd_lock) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_sd_lock = xSemaphoreCreateMutex();
    if (!s_lock || !s_sd_lock) {
        if (s_lock) vSemaphoreDelete(s_lock);
        if (s_sd_lock) vSemaphoreDelete(s_sd_lock);
        s_lock = s_sd_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(s_sd_lock, portMAX_DELAY);
    esp_err_t result = mount_sd_card();
    xSemaphoreGive(s_sd_lock);
    return result;
}

void storage_service_get_status(storage_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!s_lock) { strlcpy(status->sd_error, "Storage is not initialized", sizeof(status->sd_error)); return; }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);
}

bool storage_service_is_busy(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = s_session_active || s_file_stream_active;
    xSemaphoreGive(s_lock);
    return busy;
}

esp_err_t storage_service_claim_session(void)
{
    if (!s_lock || !s_sd_lock) return ESP_ERR_INVALID_STATE;
    if (storage_service_is_busy()) return ESP_ERR_INVALID_STATE;
    if (!storage_service_lock(pdMS_TO_TICKS(15000))) return ESP_ERR_TIMEOUT;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = s_session_active || s_file_stream_active;
    if (!busy) s_session_active = true;
    xSemaphoreGive(s_lock);
    storage_service_unlock();
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void storage_service_release_session(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_session_active = false;
    xSemaphoreGive(s_lock);
}

bool storage_service_lock(TickType_t wait)
{ return s_sd_lock && xSemaphoreTake(s_sd_lock, wait) == pdTRUE; }
void storage_service_unlock(void) { xSemaphoreGive(s_sd_lock); }
esp_err_t storage_service_mount_locked(void) { return mount_sd_card(); }
void storage_service_unmount_locked(void) { unmount_sd_card_locked(); }
