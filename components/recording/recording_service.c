#include "recording_service.h"
#include "storage_service.h"
#include "app_config.h"
#include "app_settings.h"
#include "avi_writer.h"
#include "camera_service.h"
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#define RECORDING_STOP_BIT BIT0
#define RECORDING_FRAME_PERIOD_US (1000000 / APP_RECORD_FPS)
#define RECORDING_FRAME_RETRY_COUNT 3
#define RECORDING_FRAME_RETRY_DELAY_MS 20
static const char *TAG = "recording";
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static recording_status_t s_status;
static bool state_is_busy(recording_state_t state)
{
    return state == RECORDING_STATE_STARTING || state == RECORDING_STATE_ACTIVE ||
           state == RECORDING_STATE_STOPPING;
}

const char *recording_service_state_name(recording_state_t state)
{
    switch (state) {
        case RECORDING_STATE_IDLE:
            return "idle";
        case RECORDING_STATE_STARTING:
            return "starting";
        case RECORDING_STATE_ACTIVE:
            return "active";
        case RECORDING_STATE_STOPPING:
            return "stopping";
        case RECORDING_STATE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

static void set_error_locked(const char *message)
{
    s_status.state = RECORDING_STATE_ERROR;
    strlcpy(s_status.last_error, message, sizeof(s_status.last_error));
}

static const char *recording_file_suffix(const char *name)
{
    if (!name || strncmp(name, "record_", 7) != 0) {
        return NULL;
    }
    const char *cursor = name + 7;
    const char *first_digit = cursor;
    while (isdigit((unsigned char)*cursor)) {
        ++cursor;
    }
    if (cursor == first_digit) {
        return NULL;
    }

    // Accept the previous record_<uptime>_<four digits> format while cards
    // transition to the new record_<sequence> format.
    if (*cursor == '_') {
        ++cursor;
        for (int digit = 0; digit < 4; ++digit, ++cursor) {
            if (!isdigit((unsigned char)*cursor)) {
                return NULL;
            }
        }
    }
    if (strcmp(cursor, ".avi") == 0 || strcmp(cursor, ".avi.part") == 0 ||
        strcmp(cursor, ".avi.part.idx") == 0) {
        return cursor;
    }
    return NULL;
}

static void cleanup_stale_recording_files(void)
{
    DIR *directory = opendir(APP_SD_MOUNT_POINT);
    if (!directory) {
        ESP_LOGW(TAG, "Unable to inspect stale recording files");
        return;
    }

    unsigned int removed = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        const char *name = entry->d_name;
        const char *suffix = recording_file_suffix(name);
        if (!suffix) {
            continue;
        }

        char path[320];
        int length = snprintf(path, sizeof(path), APP_SD_MOUNT_POINT "/%s", name);
        if (length < 0 || (size_t)length >= sizeof(path)) {
            continue;
        }
        if (strcmp(suffix, ".avi.part") == 0 || strcmp(suffix, ".avi.part.idx") == 0) {
            if (remove(path) == 0) {
                ++removed;
                ESP_LOGW(TAG, "Removed stale recording file: %s", path);
            }
            continue;
        }
        if (strcmp(suffix, ".avi") == 0) {
            avi_writer_repair_legacy_index(path);
        }
    }
    closedir(directory);
    if (removed > 0) {
        ESP_LOGI(TAG, "Removed %u stale recording file(s)", removed);
    }
}

static void update_progress(const avi_writer_t *writer)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_status.path, avi_writer_path(writer), sizeof(s_status.path));
    s_status.current_frames = avi_writer_frame_count(writer);
    xSemaphoreGive(s_lock);
}

static void recording_task(void *arg)
{
    uint16_t segment_minutes = (uint16_t)(uintptr_t)arg;
    const int64_t segment_duration_us = (int64_t)segment_minutes * 60 * 1000000;
    avi_writer_t writer;
    avi_writer_reset(&writer);
    int64_t segment_started_at = 0;
    bool failed = false;
    char last_completed_path[sizeof(writer.final_path)] = {0};
    const char *failure = "";
    int64_t next_frame_at = esp_timer_get_time();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool stop_requested = (s_status.state == RECORDING_STATE_STOPPING) ||
                          (xEventGroupGetBits(s_events) & RECORDING_STOP_BIT);
    if (!stop_requested) {
        s_status.state = RECORDING_STATE_ACTIVE;
    }
    xSemaphoreGive(s_lock);

    while (!stop_requested && !(xEventGroupGetBits(s_events) & RECORDING_STOP_BIT)) {
        camera_fb_t *frame = NULL;
        for (int attempt = 0; attempt < RECORDING_FRAME_RETRY_COUNT; ++attempt) {
            frame = camera_service_get_frame();
            if (frame || (xEventGroupGetBits(s_events) & RECORDING_STOP_BIT)) {
                break;
            }
            xEventGroupWaitBits(s_events, RECORDING_STOP_BIT, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS(RECORDING_FRAME_RETRY_DELAY_MS));
        }
        if (!frame) {
            if (xEventGroupGetBits(s_events) & RECORDING_STOP_BIT) {
                break;
            }
            failure = "camera frame capture failed";
            failed = true;
            break;
        }

        if (xEventGroupGetBits(s_events) & RECORDING_STOP_BIT) {
            camera_service_return_frame(frame);
            break;
        }

        storage_service_lock(portMAX_DELAY);
        bool storage_error = false;
        if (avi_writer_would_exceed_limit(&writer, frame->len)) {
            uint32_t frames = avi_writer_frame_count(&writer);
            char path[sizeof(writer.final_path)];
            strlcpy(path, avi_writer_path(&writer), sizeof(path));
            if (!avi_writer_finalize(&writer)) {
                storage_error = true;
            } else {
                strlcpy(last_completed_path, path, sizeof(last_completed_path));
                ESP_LOGI(TAG, "Recording file size limit reached: %s (%" PRIu32 " frames)", path, frames);
                avi_writer_reset(&writer);
                segment_started_at = esp_timer_get_time();
            }
        }
        bool written = !storage_error &&
                       (avi_writer_is_open(&writer)
                            ? avi_writer_write_frame(&writer, frame)
                            : avi_writer_open(&writer, frame) && avi_writer_write_frame(&writer, frame));
        storage_service_unlock();
        camera_service_return_frame(frame);
        if (!written) {
            storage_service_lock(portMAX_DELAY);
            avi_writer_abort(&writer);
            storage_service_unmount_locked();
            storage_service_unlock();
            failure = storage_error ? "AVI finalization failed" : "SD card write failed";
            failed = true;
            break;
        }

        if (segment_started_at == 0) {
            segment_started_at = esp_timer_get_time();
        }
        update_progress(&writer);

        if (esp_timer_get_time() - segment_started_at >= segment_duration_us) {
            uint32_t frames = avi_writer_frame_count(&writer);
            char path[sizeof(writer.final_path)];
            strlcpy(path, avi_writer_path(&writer), sizeof(path));
            storage_service_lock(portMAX_DELAY);
            bool finalized = avi_writer_finalize(&writer);
            storage_service_unlock();
            if (!finalized) {
                storage_service_lock(portMAX_DELAY);
                storage_service_unmount_locked();
                storage_service_unlock();
                failure = "AVI finalization failed";
                failed = true;
                break;
            }
            ESP_LOGI(TAG, "Recording segment complete: %s (%" PRIu32 " frames)", path, frames);
            strlcpy(last_completed_path, path, sizeof(last_completed_path));
            avi_writer_reset(&writer);
            segment_started_at = 0;
            update_progress(&writer);
        }

        int64_t now = esp_timer_get_time();
        if (now - next_frame_at > RECORDING_FRAME_PERIOD_US * 4) {
            next_frame_at = now;
        }
        next_frame_at += RECORDING_FRAME_PERIOD_US;
        int64_t wait_us = next_frame_at - now;
        if (wait_us > 0) {
            xEventGroupWaitBits(s_events, RECORDING_STOP_BIT, pdFALSE, pdFALSE,
                                pdMS_TO_TICKS((wait_us + 999) / 1000));
        }
    }

    if (avi_writer_is_open(&writer)) {
        uint32_t frames = avi_writer_frame_count(&writer);
        char path[sizeof(writer.final_path)];
        strlcpy(path, avi_writer_path(&writer), sizeof(path));
        storage_service_lock(portMAX_DELAY);
        bool finalized = avi_writer_finalize(&writer);
        storage_service_unlock();
        if (!finalized) {
            storage_service_lock(portMAX_DELAY);
            storage_service_unmount_locked();
            storage_service_unlock();
            failure = "AVI finalization failed";
            failed = true;
        } else {
            strlcpy(last_completed_path, path, sizeof(last_completed_path));
            ESP_LOGI(TAG, "Recording complete: %s (%" PRIu32 " frames)", path, frames);
        }
    }
    if (failed) {
        storage_service_lock(portMAX_DELAY);
        avi_writer_abort(&writer);
        storage_service_unlock();
        ESP_LOGE(TAG, "%s", failure);
    }

    camera_service_end_recording();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.current_frames = 0;
    if (failed) {
        strlcpy(s_status.path, last_completed_path, sizeof(s_status.path));
        set_error_locked(failure);
    } else {
        s_status.state = RECORDING_STATE_IDLE;
        s_status.path[0] = '\0';
        s_status.last_error[0] = '\0';
    }
    xSemaphoreGive(s_lock);
    storage_service_release_session();
    vTaskDelete(NULL);
}

esp_err_t recording_service_init(void)
{
    if (s_lock && s_events) return ESP_OK;
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) {
        if (s_lock) vSemaphoreDelete(s_lock);
        if (s_events) vEventGroupDelete(s_events);
        s_lock = NULL; s_events = NULL;
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = RECORDING_STATE_IDLE;
    s_status.segment_minutes = app_settings_load_segment_minutes();
    return ESP_OK;
}

esp_err_t recording_service_start(void)
{
    if (!s_lock || !s_events || !camera_service_is_ready()) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (state_is_busy(s_status.state)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = RECORDING_STATE_STARTING;
    s_status.path[0] = s_status.last_error[0] = '\0';
    s_status.current_frames = 0;
    uint16_t minutes = s_status.segment_minutes;
    xEventGroupClearBits(s_events, RECORDING_STOP_BIT);
    xSemaphoreGive(s_lock);
    esp_err_t result = storage_service_claim_session();
    bool claimed = result == ESP_OK;
    if (claimed) {
        storage_service_lock(portMAX_DELAY);
        result = storage_service_mount_locked();
        if (result == ESP_OK) cleanup_stale_recording_files();
        storage_service_unlock();
    }
    if (result == ESP_OK && (xEventGroupGetBits(s_events) & RECORDING_STOP_BIT))
        result = ESP_ERR_INVALID_STATE;
    if (result == ESP_OK) result = camera_service_begin_recording();
    if (result == ESP_OK && xTaskCreate(recording_task, "camera_record", 6144,
                                      (void *)(uintptr_t)minutes, 5, NULL) != pdPASS) {
        camera_service_end_recording();
        result = ESP_ERR_NO_MEM;
    }
    if (result != ESP_OK) {
        if (claimed) storage_service_release_session();
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_error_locked(esp_err_to_name(result));
        xSemaphoreGive(s_lock);
    }
    return result;
}

esp_err_t recording_service_stop(void)
{
    if (!s_lock || !s_events) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!state_is_busy(s_status.state)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = RECORDING_STATE_STOPPING;
    xSemaphoreGive(s_lock);
    xEventGroupSetBits(s_events, RECORDING_STOP_BIT);
    return ESP_OK;
}

esp_err_t recording_service_set_segment_minutes(uint16_t minutes)
{
    if (!s_lock || minutes < APP_RECORD_SEGMENT_MINUTES_MIN || minutes > APP_RECORD_SEGMENT_MINUTES_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (state_is_busy(s_status.state)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = app_settings_save_segment_minutes(minutes);
    if (result == ESP_OK) {
        s_status.segment_minutes = minutes;
    }
    xSemaphoreGive(s_lock);
    return result;
}

void recording_service_get_status(recording_status_t *status)
{
    if (!status) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (!s_lock) {
        status->state = RECORDING_STATE_ERROR;
        strlcpy(status->last_error, "recording service unavailable", sizeof(status->last_error));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);
}

bool recording_service_is_busy(void)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool busy = state_is_busy(s_status.state);
    xSemaphoreGive(s_lock);
    return busy;
}

esp_err_t recording_service_stop_and_wait(uint32_t timeout_ms)
{
    if (!s_lock || !s_events) return ESP_OK;
    if (!recording_service_is_busy()) return ESP_OK;
    recording_service_stop();
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    while (recording_service_is_busy()) {
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    recording_status_t status;
    recording_service_get_status(&status);
    return status.state == RECORDING_STATE_ERROR ? ESP_FAIL : ESP_OK;
}
