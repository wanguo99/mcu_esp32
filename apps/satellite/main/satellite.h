#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "angle_search.h"
#include "esp_err.h"
#include "power_monitor.h"
#define SATELLITE_AXES 3
#define SATELLITE_MONITORS 4
#define SATELLITE_DEFAULT_MOVE_MS 500U
#define SATELLITE_SERVO_TIMER 1
#define SATELLITE_SERVO_CHANNEL(axis) ((axis)+1)
typedef struct {
    const char *backend;
    bool hardware_readback,output_enabled;
    int gpio;
    uint32_t frequency_hz,compare_ticks,period_ticks;
} satellite_pwm_status_t;
typedef enum { SAT_MODE_MANUAL, SAT_MODE_AUTO, SAT_MODE_DEMO, SAT_MODE_SCAN } satellite_mode_t;
typedef struct {
    bool configured,ready,output_enabled,angle_known,moving,home_known,auto_released;
    int gpio;
    float angle,target,home,max_angle;
    satellite_mode_t mode;
    int search_phase;
    float best_angle,best_score;
    bool best_valid;
    esp_err_t error;
} satellite_axis_status_t;
typedef struct {
    satellite_axis_status_t axis[SATELLITE_AXES];
    power_monitor_t power[SATELLITE_MONITORS];
    esp_err_t voltage_error[SATELLITE_MONITORS],current_error[SATELLITE_MONITORS];
    bool standby,battery_configured,low_battery,power_unknown,pose_fault;
    bool wifi_connected,imu_valid;
    int rssi_dbm;
    uint8_t bssid[6];
    float tilt_deg,accel_m_s2[3],gyro_dps[3],temperature_c;
    int64_t sample_us;
    esp_err_t imu_error,fan_error,led_error,relay_error;
    bool fan_ready,led_ready,relay_ready,fan_rpm_valid;
    float fan_rpm;
    unsigned fan_duty;
} satellite_status_t;
// App composition owns the hardware; all public calls serialize against its control task.
esp_err_t satellite_start(void);
esp_err_t satellite_get_status(satellite_status_t *out);
esp_err_t satellite_get_pwm_status(unsigned axis,satellite_pwm_status_t *out);
// Copies the latest scan trace; caller owns the buffer. No samples are synthesized.
esp_err_t satellite_get_scan(unsigned axis,angle_search_point_t *points,size_t capacity,size_t *count);
// Axis: 0 antenna, 1 left solar, 2 right solar. angle is the unified angle-setting operation.
// Low-level set/move and by/home/record/limit/hold/stop/scan/auto/demo/manual remain available.
esp_err_t satellite_axis_command(unsigned axis,const char *action,float value,uint32_t duration_ms);
// Global: standby/resume; fan value=0..100; led value=0..255; relay channel/value=0 or 1.
esp_err_t satellite_command(const char *action,unsigned channel,float value);
