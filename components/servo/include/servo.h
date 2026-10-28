#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct servo_instance *servo_handle_t;
typedef struct {
    int gpio, timer, channel; // timer/channel select LEDC on non-ESP32 targets; ESP32 allocates MCPWM resources.
    uint32_t frequency_hz, min_pulse_us, max_pulse_us;
    float min_angle, max_angle;
    uint32_t refresh_interval_ms; // 0 selects 20 ms for background motion.
    bool reversed; // Reverse logical angle-to-pulse mapping; PWM polarity stays active-high.
    uint32_t auto_release_ms; // Idle target hold before PWM goes low; 0 keeps PWM enabled.
} servo_config_t;

#define SERVO_CONFIG_DEFAULT(output_pin, timer_id, channel_id) { \
    .gpio=(output_pin), .timer=(timer_id), .channel=(channel_id), .frequency_hz=50, \
    .min_pulse_us=500, .max_pulse_us=2500, .min_angle=0, .max_angle=180, .refresh_interval_ms=20, .reversed=false \
}
typedef enum { SERVO_CURVE_LINEAR, SERVO_CURVE_SMOOTH } servo_curve_t;
typedef struct { float angle; uint32_t duration_ms, hold_ms; } servo_step_t;
typedef struct {
    bool output_enabled, angle_known, moving, auto_released;
    bool home_known;
    float home_angle, min_angle, max_angle; // Saved home and software limits, not calibration endpoints.
    float commanded_angle, target_angle; // Command estimates, never hardware feedback.
    uint32_t step_index, step_remaining_ms;
    esp_err_t last_error;
} servo_status_t;

typedef struct {
    const char *backend;
    bool hardware_readback, output_enabled;
    uint32_t frequency_hz, compare_ticks, period_ticks;
} servo_pwm_status_t;

// Creates a stopped PWM output. The application supplies calibrated limits and resources.
esp_err_t servo_create(const servo_config_t *config, servo_handle_t *out);
esp_err_t servo_set_angle(servo_handle_t servo, float angle);
esp_err_t servo_set_pulse_us(servo_handle_t servo, uint32_t pulse_us);
// Establish a start with set_angle first. Auto-release preserves that command for later moves.
// Explicit stop requires a fresh set_angle. duration=0 applies immediately.
esp_err_t servo_move_to(servo_handle_t servo, float angle, uint32_t duration_ms, servo_curve_t curve);
// Relative to the current command (not the previous target); no wrapping or clamping.
esp_err_t servo_move_by(servo_handle_t servo, float delta_angle, uint32_t duration_ms, servo_curve_t curve);
// Record the idle command as home (including auto-released); retained until destruction.
esp_err_t servo_record_home(servo_handle_t servo);
esp_err_t servo_return_home(servo_handle_t servo, uint32_t duration_ms, servo_curve_t curve);
// Restrict motion inside calibrated limits without changing angle-to-pulse mapping.
// Requires idle state; current command and saved home must remain inside the new range.
esp_err_t servo_set_limits(servo_handle_t servo, float min_angle, float max_angle);
// Smooth trajectory with bounded commanded speed/acceleration; requires both >0.
esp_err_t servo_move_with_limits(servo_handle_t servo, float angle, float speed_dps, float acceleration_dps2);
// Copies up to 32 steps. duration=1..600000 ms; repeat_count=0 loops indefinitely.
esp_err_t servo_play_sequence(servo_handle_t servo, const servo_step_t *steps, uint32_t count,
                               uint32_t repeat_count, servo_curve_t curve);
esp_err_t servo_sweep(servo_handle_t servo, float min_angle, float max_angle,
                       uint32_t travel_ms, uint32_t hold_ms);
esp_err_t servo_get_status(servo_handle_t servo, servo_status_t *out);
// MCPWM reports the commanded comparator value; LEDC exposes a hardware readback.
esp_err_t servo_get_pwm_status(servo_handle_t servo, servo_pwm_status_t *out);
esp_err_t servo_wait_until_idle(servo_handle_t servo, uint32_t timeout_ms);
// Cancel motion while retaining the latest commanded PWM, or stop PWM entirely.
esp_err_t servo_cancel_motion(servo_handle_t servo);
esp_err_t servo_stop(servo_handle_t servo);
// Exclude concurrent callers before destroy; motion worker is joined internally.
esp_err_t servo_destroy(servo_handle_t servo);
