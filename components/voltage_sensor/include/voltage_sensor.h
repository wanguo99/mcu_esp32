#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct voltage_sensor *voltage_sensor_handle_t;
typedef struct {
    int gpio; // ADC1 only. All channels share one managed ADC unit.
    uint32_t upper_ohms, lower_ohms; // Divider; upper=0 permits direct low-voltage sampling.
    unsigned samples; // 1..64, averaged with calibration.
    float gain, offset_v; // Final measured circuit voltage = divided reading * gain + offset.
} voltage_sensor_config_t;
#define VOLTAGE_SENSOR_CONFIG_DEFAULT(pin) { .gpio=(pin), .upper_ohms=30000, .lower_ohms=10000, .samples=16, .gain=1 }
typedef struct { float voltage_v, adc_voltage_v; int64_t timestamp_us; } voltage_sensor_sample_t;
esp_err_t voltage_sensor_create(const voltage_sensor_config_t *config, voltage_sensor_handle_t *out);
// Reject saturation; on error output is unchanged. Values are not fabricated if calibration is unavailable.
esp_err_t voltage_sensor_read(voltage_sensor_handle_t sensor, voltage_sensor_sample_t *out);
// Exclude concurrent users of this handle before destruction.
esp_err_t voltage_sensor_destroy(voltage_sensor_handle_t sensor);
