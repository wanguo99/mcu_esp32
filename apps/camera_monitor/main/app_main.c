#include "esp_log.h"
#include "runtime.h"
void app_main(void) {
    esp_err_t result = runtime_start();
    if (result != ESP_OK) ESP_LOGE("camera_monitor", "Service startup failed: %s", esp_err_to_name(result));
}
