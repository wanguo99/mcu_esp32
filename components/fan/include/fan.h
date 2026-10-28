#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct fan_instance *fan_handle_t;
typedef struct {
    int pwm_gpio, tach_gpio; // tach_gpio=-1 for PWM-only operation.
    int timer, channel;     // Low-speed LEDC resources, explicit and unique.
    uint32_t pwm_frequency_hz; // 0 selects 25 kHz; check the fan specification.
    uint32_t sample_period_ms; // 0 selects 1000 ms, otherwise 100..5000.
    uint16_t pulses_per_revolution; // Required when tach is configured; often 2.
    bool pwm_inverted; // E.g. an external inverting/open-collector driver.
} fan_config_t;
#define FAN_CONFIG_DEFAULT(pwm_pin, tach_pin, timer_id, channel_id) { \
    .pwm_gpio=(pwm_pin), .tach_gpio=(tach_pin), .timer=(timer_id), .channel=(channel_id), \
    .pwm_frequency_hz=25000, .sample_period_ms=1000, .pulses_per_revolution=2, .pwm_inverted=false \
}
typedef struct {
    unsigned duty_percent; // Requested duty, independent of measured speed.
    bool tach_available, rpm_valid;
    float rpm;
    uint32_t sample_age_ms;
} fan_status_t;

// Starts at logical 0% PWM. A fan may keep spinning at 0%; this is not a power switch.
esp_err_t fan_create(const fan_config_t *config, fan_handle_t *out);
esp_err_t fan_set_duty(fan_handle_t fan, unsigned percent);
esp_err_t fan_stop(fan_handle_t fan); // Set PWM to 0%; continue measuring coasting.
esp_err_t fan_get_status(fan_handle_t fan, fan_status_t *out);
// Call from a normal task; ensure no other caller uses the handle during destroy.
esp_err_t fan_destroy(fan_handle_t fan);
