#include "power_monitor.h"
#include <math.h>
esp_err_t power_monitor_update(power_monitor_t *s, int64_t now, bool vv, float v, bool iv, float i, uint32_t gap) {
    if (!s || now<0 || !gap || (s->timestamp_us && now<=s->timestamp_us)) return ESP_ERR_INVALID_ARG;
    vv=vv && isfinite(v) && v>=0; iv=iv && isfinite(i);
    bool valid=vv && iv && isfinite(v*i);
    float power=valid?v*i:0;
    if (valid && s->integrating && now-s->timestamp_us<=(int64_t)gap*1000)
        s->energy_wh+=((double)s->previous_power+power)*0.5*(now-s->timestamp_us)/3600000000.0;
    s->voltage_valid=vv; s->power_valid=valid; s->voltage_v=vv?v:0;
    s->current_a=valid?i:0; s->power_w=power; s->timestamp_us=now;
    s->integrating=valid; s->previous_power=power; return ESP_OK;
}
bool power_monitor_low(bool previous,float v,float low,float recovery) {
    return previous ? v<recovery : v<=low;
}
