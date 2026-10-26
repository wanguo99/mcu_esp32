#include "runtime.h"
#include "board.h"
#include "settings.h"
#include "sdkconfig.h"
#include "esp_log.h"
#if CONFIG_MCU_ENABLE_NETWORK
#include "wifi_manager.h"
#endif
#if CONFIG_MCU_ENABLE_WEB
#include "web_server.h"
#endif
#if CONFIG_MCU_ENABLE_CAMERA
#include "camera_service.h"
#endif
#if CONFIG_MCU_ENABLE_STORAGE
#include "storage_service.h"
#endif
#if CONFIG_MCU_ENABLE_RECORDING
#include "recording_service.h"
#endif
#if CONFIG_MCU_ENABLE_CAMERA && CONFIG_MCU_ENABLE_WEB
#include "monitor_web.h"
#endif

static const char *TAG = "runtime";

esp_err_t runtime_prepare_restart(void)
{
#if CONFIG_MCU_ENABLE_CAMERA && CONFIG_MCU_ENABLE_WEB
    return monitor_web_prepare_restart();
#else
#if CONFIG_MCU_ENABLE_RECORDING
    esp_err_t result = recording_service_stop_and_wait(15000);
    if (result != ESP_OK) return result;
#endif
#if CONFIG_MCU_ENABLE_CAMERA
    return camera_service_set_enabled(false);
#else
    return ESP_OK;
#endif
#endif
}

esp_err_t runtime_start(void)
{
    esp_err_t result = settings_init();
    if (result != ESP_OK) return result;
    const board_config_t *board = board_get_config();
    ESP_LOGI(TAG, "Board %s: Flash %u MB, PSRAM %u MB (profile)",
             board->name, board->flash_mb, board->psram_mb);
#if CONFIG_MCU_ENABLE_CAMERA
    result = camera_service_init();
    if (result != ESP_OK) ESP_LOGE(TAG, "Camera service unavailable: %s", esp_err_to_name(result));
#endif
#if CONFIG_MCU_ENABLE_STORAGE
    result = storage_service_init();
    if (result != ESP_OK) ESP_LOGW(TAG, "Storage unavailable: %s", esp_err_to_name(result));
#endif
#if CONFIG_MCU_ENABLE_RECORDING
    result = recording_service_init();
    if (result != ESP_OK) ESP_LOGE(TAG, "Recording unavailable: %s", esp_err_to_name(result));
#endif
#if CONFIG_MCU_ENABLE_NETWORK
    result = wifi_manager_init();
    if (result != ESP_OK) return result;
#endif
#if CONFIG_MCU_ENABLE_WEB
    result = web_server_start(runtime_prepare_restart);
    if (result != ESP_OK) return result;
#endif
#if CONFIG_MCU_ENABLE_CAMERA && CONFIG_MCU_ENABLE_WEB
    result = monitor_web_start();
    if (result != ESP_OK) return result;
#endif
    ESP_LOGI(TAG, "Selected services started; actuator outputs remain disabled");
    return ESP_OK;
}
