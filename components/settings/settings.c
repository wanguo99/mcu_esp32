#include "settings.h"
#include "nvs.h"
#include "nvs_flash.h"

esp_err_t settings_init(void) { return nvs_flash_init(); }

uint16_t settings_get_u16(const char *name_space, const char *key, uint16_t fallback)
{
    nvs_handle_t handle;
    uint16_t value = fallback;
    if (nvs_open(name_space, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u16(handle, key, &value);
        nvs_close(handle);
    }
    return value;
}

esp_err_t settings_set_u16(const char *name_space, const char *key, uint16_t value)
{
    nvs_handle_t handle;
    esp_err_t result = nvs_open(name_space, NVS_READWRITE, &handle);
    if (result != ESP_OK) return result;
    result = nvs_set_u16(handle, key, value);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result;
}
