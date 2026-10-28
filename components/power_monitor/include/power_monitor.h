#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool voltage_valid, power_valid;
    float voltage_v, current_a, power_w;
    double energy_wh;
    int64_t timestamp_us;
    bool integrating;
    float previous_power;
} power_monitor_t;
// Call once per fresh voltage/current sample. Invalid data breaks integration, not cumulative energy.
esp_err_t power_monitor_update(power_monitor_t *state, int64_t now_us, bool voltage_valid, float voltage,
                                bool current_valid, float current, uint32_t max_gap_ms);
// Hysteresis preserves previous low state until recovery; caller handles missing/stale samples separately.
bool power_monitor_low(bool previous, float voltage, float low_v, float recovery_v);
