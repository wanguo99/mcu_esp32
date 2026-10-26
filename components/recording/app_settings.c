#include "app_settings.h"
#include "settings.h"
uint16_t app_settings_load_segment_minutes(void)
{
    uint16_t value = settings_get_u16(APP_RECORD_NVS_NAMESPACE, APP_RECORD_NVS_SEGMENT_KEY,
                                     APP_RECORD_SEGMENT_MINUTES_DEFAULT);
    return value >= APP_RECORD_SEGMENT_MINUTES_MIN && value <= APP_RECORD_SEGMENT_MINUTES_MAX
               ? value : APP_RECORD_SEGMENT_MINUTES_DEFAULT;
}
esp_err_t app_settings_save_segment_minutes(uint16_t minutes)
{
    if (minutes < APP_RECORD_SEGMENT_MINUTES_MIN || minutes > APP_RECORD_SEGMENT_MINUTES_MAX)
        return ESP_ERR_INVALID_ARG;
    return settings_set_u16(APP_RECORD_NVS_NAMESPACE, APP_RECORD_NVS_SEGMENT_KEY, minutes);
}
