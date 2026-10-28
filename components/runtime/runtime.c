#include "runtime.h"
#include "board.h"
#include "settings.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
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
static runtime_shutdown_hook_t s_hooks[8];
static unsigned s_hook_count;
static portMUX_TYPE s_hook_lock = portMUX_INITIALIZER_UNLOCKED;

esp_err_t runtime_register_shutdown_hook(runtime_shutdown_hook_t hook)
{
    if (!hook) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&s_hook_lock);
    for (unsigned i=0; i<s_hook_count; ++i) {
        if (s_hooks[i] == hook) { portEXIT_CRITICAL(&s_hook_lock); return ESP_OK; }
    }
    esp_err_t result = ESP_ERR_NO_MEM;
    if (s_hook_count < 8) { s_hooks[s_hook_count++] = hook; result = ESP_OK; }
    portEXIT_CRITICAL(&s_hook_lock);
    return result;
}

static esp_err_t prepare_services(void)
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

esp_err_t runtime_prepare_restart(void)
{
    runtime_shutdown_hook_t hooks[8];
    portENTER_CRITICAL(&s_hook_lock);
    unsigned count = s_hook_count;
    for (unsigned i=0; i<count; ++i) hooks[i] = s_hooks[i];
    portEXIT_CRITICAL(&s_hook_lock);
    for (unsigned i=0; i<count; ++i) {
        esp_err_t result = hooks[i]();
        if (result != ESP_OK) return result;
    }
    return prepare_services();
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
