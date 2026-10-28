#include "voltage_sensor.h"
#include "peripheral_resources.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdlib.h>
struct voltage_sensor {
    voltage_sensor_config_t config;
    adc_channel_t channel;
    adc_cali_handle_t calibration;
    peripheral_claim_t claim;
};
static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex;
static portMUX_TYPE s_init_lock=portMUX_INITIALIZER_UNLOCKED;
static adc_oneshot_unit_handle_t s_unit;
static unsigned s_users;
static void lock(void) {
    portENTER_CRITICAL(&s_init_lock);
    if (!s_mutex) s_mutex=xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    portEXIT_CRITICAL(&s_init_lock);
    xSemaphoreTake(s_mutex,portMAX_DELAY);
}
esp_err_t voltage_sensor_create(const voltage_sensor_config_t *c, voltage_sensor_handle_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    if (!c || !c->lower_ohms || !c->samples || c->samples>64 || !isfinite(c->gain) || c->gain<=0 || !isfinite(c->offset_v)) return ESP_ERR_INVALID_ARG;
    adc_unit_t unit; adc_channel_t channel;
    esp_err_t err=adc_oneshot_io_to_channel(c->gpio,&unit,&channel);
    if (err!=ESP_OK || unit!=ADC_UNIT_1) return ESP_ERR_INVALID_ARG;
    voltage_sensor_handle_t s=calloc(1,sizeof(*s));
    if (!s) return ESP_ERR_NO_MEM;
    s->config=*c; s->channel=channel;
    s->claim=(peripheral_claim_t){.gpios=UINT64_C(1)<<c->gpio,.ledc_timer=-1,.ledc_channel=-1};
    err=peripheral_resources_acquire(&s->claim);
    if (err!=ESP_OK) { free(s); return err; }
    lock();
    if (!s_unit) {
        adc_oneshot_unit_init_cfg_t cfg={.unit_id=ADC_UNIT_1};
        err=adc_oneshot_new_unit(&cfg,&s_unit);
    }
    if (err==ESP_OK) {
        adc_oneshot_chan_cfg_t cfg={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
        err=adc_oneshot_config_channel(s_unit,channel,&cfg);
    }
    if (err==ESP_OK) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_curve_fitting_config_t cfg={.unit_id=ADC_UNIT_1,.chan=channel,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12};
        err=adc_cali_create_scheme_curve_fitting(&cfg,&s->calibration);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cfg={.unit_id=ADC_UNIT_1,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_12,.default_vref=0};
        err=adc_cali_create_scheme_line_fitting(&cfg,&s->calibration);
#else
        err=ESP_ERR_NOT_SUPPORTED;
#endif
    }
    if (err==ESP_OK) { ++s_users; *out=s; }
    else if (!s_users && s_unit) { adc_oneshot_del_unit(s_unit); s_unit=NULL; }
    xSemaphoreGive(s_mutex);
    if (err!=ESP_OK) { peripheral_resources_release(&s->claim); free(s); }
    return err;
}
esp_err_t voltage_sensor_read(voltage_sensor_handle_t s, voltage_sensor_sample_t *out) {
    if (!s || !out) return ESP_ERR_INVALID_ARG;
    lock(); int sum=0; esp_err_t err=ESP_OK;
    for (unsigned i=0;i<s->config.samples;++i) {
        int raw,mv; err=adc_oneshot_read(s_unit,s->channel,&raw);
        if (err!=ESP_OK) break;
        if (raw>=4090) { err=ESP_ERR_INVALID_RESPONSE; break; }
        err=adc_cali_raw_to_voltage(s->calibration,raw,&mv);
        if (err!=ESP_OK) break;
        sum+=mv;
    }
    if (err==ESP_OK) {
        voltage_sensor_sample_t v={.adc_voltage_v=(float)sum/s->config.samples/1000,.timestamp_us=esp_timer_get_time()};
        v.voltage_v=v.adc_voltage_v*(1.0+(double)s->config.upper_ohms/s->config.lower_ohms)*s->config.gain+s->config.offset_v;
        if (!isfinite(v.voltage_v)) err=ESP_ERR_INVALID_RESPONSE;
        else *out=v;
    }
    xSemaphoreGive(s_mutex); return err;
}
esp_err_t voltage_sensor_destroy(voltage_sensor_handle_t s) {
    if (!s) return ESP_ERR_INVALID_ARG;
    lock();
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    esp_err_t err=adc_cali_delete_scheme_curve_fitting(s->calibration);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    esp_err_t err=adc_cali_delete_scheme_line_fitting(s->calibration);
#else
    esp_err_t err=ESP_ERR_NOT_SUPPORTED;
#endif
    if (err==ESP_OK) {
        --s_users;
        if (!s_users) { adc_oneshot_del_unit(s_unit); s_unit=NULL; }
    }
    xSemaphoreGive(s_mutex);
    if (err==ESP_OK) { peripheral_resources_release(&s->claim); free(s); }
    return err;
}
