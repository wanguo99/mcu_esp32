#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {
    RECORDING_STATE_IDLE, RECORDING_STATE_STARTING, RECORDING_STATE_ACTIVE,
    RECORDING_STATE_STOPPING, RECORDING_STATE_ERROR,
} recording_state_t;
typedef struct {
    recording_state_t state;
    uint16_t segment_minutes;
    uint32_t current_frames;
    char path[96], last_error[64];
} recording_status_t;
esp_err_t recording_service_init(void);
esp_err_t recording_service_start(void);
esp_err_t recording_service_stop(void);
esp_err_t recording_service_stop_and_wait(uint32_t timeout_ms);
esp_err_t recording_service_set_segment_minutes(uint16_t minutes);
void recording_service_get_status(recording_status_t *status);
bool recording_service_is_busy(void);
const char *recording_service_state_name(recording_state_t state);
