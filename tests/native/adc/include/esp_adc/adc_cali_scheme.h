#pragma once
#ifndef ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
#endif
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED (!ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED)
#include "adc_cali.h"
typedef struct { int unit_id,chan,atten,bitwidth; } adc_cali_curve_fitting_config_t;
esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *c,adc_cali_handle_t *out);
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t c);

typedef struct { int unit_id,atten,bitwidth,default_vref; } adc_cali_line_fitting_config_t;
esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *c,adc_cali_handle_t *out);
esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t c);
