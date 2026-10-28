#include "runtime.h"
#include "console.h"
#include "esp_log.h"
void app_main(void)
{
    esp_err_t result = runtime_start();
    if (result == ESP_OK) result = component_console_start();
    if (result != ESP_OK) ESP_LOGE("component_demo", "Startup failed: %s", esp_err_to_name(result));
}
