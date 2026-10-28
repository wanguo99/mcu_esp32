#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_app_desc.h"
#include "board.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "runtime.h"
#include "satellite.h"
#include "sdkconfig.h"
esp_err_t satellite_console_start(void);
#if CONFIG_MCU_ENABLE_WEB
esp_err_t satellite_web_start(void);
#endif

static const char *TAG = "beidou";

void app_main(void)
{
    ESP_ERROR_CHECK(runtime_start());

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s v%s, ESP-IDF %s", app->project_name, app->version,
             app->idf_ver);
    ESP_LOGI(TAG, "%s: %d cores, revision %u.%u", board_get_config()->name,
             chip.cores, chip.revision / 100, chip.revision % 100);
    size_t psram_size = 0;
#if CONFIG_SPIRAM
    psram_size = esp_psram_get_size();
#endif
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB, PSRAM: %zu MB",
             flash_size / (1024 * 1024), psram_size / (1024 * 1024));
    ESP_ERROR_CHECK(satellite_start());
    ESP_ERROR_CHECK(satellite_console_start());
#if CONFIG_MCU_ENABLE_WEB
    ESP_ERROR_CHECK(satellite_web_start());
#endif
}
