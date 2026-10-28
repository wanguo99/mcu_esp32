#include "voltage_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali_scheme.h"
#include "board.h"
#include "board_definition.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
static unsigned units,calibrations,reads;
static bool fail_calibration,fail_read,saturated;
const board_config_t *board_get_config(void) { return &board_definition; }
int64_t esp_timer_get_time(void) { return 1000000; }
esp_err_t adc_oneshot_io_to_channel(int gpio,adc_unit_t *u,adc_channel_t *ch) {
    if(gpio<1 || gpio>10) return ESP_ERR_INVALID_ARG;
    *u=ADC_UNIT_1; *ch=gpio-1; return ESP_OK;
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c,adc_oneshot_unit_handle_t *out) {
    assert(c->unit_id==ADC_UNIT_1 && units==0); ++units; *out=(void*)1; return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t u,adc_channel_t ch,const adc_oneshot_chan_cfg_t *c) {
    (void)ch; assert(u && c->atten==ADC_ATTEN_DB_12); return ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t u,adc_channel_t ch,int *out) {
    (void)ch; assert(u); reads++; if(fail_read) return ESP_ERR_TIMEOUT; *out=saturated?4095:1000; return ESP_OK;
}
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t u) { assert(u && units==1); units--; return ESP_OK; }
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *c,adc_cali_handle_t *out) {
    (void)c; if(fail_calibration) return ESP_ERR_NOT_SUPPORTED;
    *out=malloc(1); calibrations++; return ESP_OK;
}
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t c) { free(c); calibrations--; return ESP_OK; }
esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t c,int raw,int *mv) { assert(c); *mv=raw; return ESP_OK; }
esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *c,adc_cali_handle_t *out) {
    assert(c->default_vref==0 && c->unit_id==ADC_UNIT_1);
    return adc_cali_create_scheme_curve_fitting(NULL,out);
}
esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t c) { return adc_cali_delete_scheme_curve_fitting(c); }
static void sharing(void) {
    voltage_sensor_handle_t a,b,duplicate;
    voltage_sensor_config_t c=VOLTAGE_SENSOR_CONFIG_DEFAULT(1);
    assert(voltage_sensor_create(&c,&a)==ESP_OK);
    assert(voltage_sensor_create(&c,&duplicate)==ESP_ERR_INVALID_STATE && !duplicate);
    c.gpio=2; assert(voltage_sensor_create(&c,&b)==ESP_OK); assert(units==1 && calibrations==2);
    assert(voltage_sensor_destroy(a)==ESP_OK && units==1);
    voltage_sensor_sample_t v; assert(voltage_sensor_read(b,&v)==ESP_OK && fabsf(v.voltage_v-4)<0.0001 && reads==16);
    assert(voltage_sensor_destroy(b)==ESP_OK && units==0 && calibrations==0);
}
static void faults(void) {
    voltage_sensor_handle_t a; voltage_sensor_config_t c=VOLTAGE_SENSOR_CONFIG_DEFAULT(1);
    fail_calibration=true; assert(voltage_sensor_create(&c,&a)==ESP_ERR_NOT_SUPPORTED && !a && units==0);
    fail_calibration=false; assert(voltage_sensor_create(&c,&a)==ESP_OK);
    voltage_sensor_sample_t out={.voltage_v=42};
    fail_read=true; assert(voltage_sensor_read(a,&out)==ESP_ERR_TIMEOUT && out.voltage_v==42);
    fail_read=false; saturated=true;
    assert(voltage_sensor_read(a,&out)==ESP_ERR_INVALID_RESPONSE && out.voltage_v==42);
    saturated=false; assert(voltage_sensor_read(a,&out)==ESP_OK);
    assert(voltage_sensor_destroy(a)==ESP_OK && units==0);
    c.gpio=19; assert(voltage_sensor_create(&c,&a)==ESP_ERR_INVALID_ARG);
    c.gpio=1; c.gain=NAN; assert(voltage_sensor_create(&c,&a)==ESP_ERR_INVALID_ARG);
}
int main(int argc,char **argv) { assert(argc==2); if(!strcmp(argv[1],"sharing"))sharing(); else faults(); }
