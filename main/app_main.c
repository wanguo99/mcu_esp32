#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "beidou";

void app_main(void)
{
    // Preserve stored configuration on NVS errors; recovery must be explicit.
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s v%s, ESP-IDF %s", app->project_name, app->version,
             app->idf_ver);
    ESP_LOGI(TAG, "ESP32-S3: %d cores, revision %d", chip.cores, chip.revision);
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB, PSRAM: %zu MB",
             flash_size / (1024 * 1024), esp_psram_get_size() / (1024 * 1024));
    ESP_LOGI(TAG, "NVS ready. Servo outputs are not configured.");

    while (true) {
        ESP_LOGI(TAG, "uptime=%" PRId64 "s, free_internal=%zu, free_psram=%zu",
                 esp_timer_get_time() / 1000000,
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
