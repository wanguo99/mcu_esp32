#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct servo_instance *servo_handle_t;
typedef struct {
    int gpio, timer, channel;
    uint32_t frequency_hz, min_pulse_us, max_pulse_us;
    float min_angle, max_angle;
} servo_config_t;

// Creates a stopped PWM output. The application supplies calibrated limits and resources.
esp_err_t servo_create(const servo_config_t *config, servo_handle_t *out);
esp_err_t servo_set_angle(servo_handle_t servo, float angle);
esp_err_t servo_stop(servo_handle_t servo);
esp_err_t servo_destroy(servo_handle_t servo);
