#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_camera.h"
#include "esp_err.h"

esp_err_t camera_service_init(void);
esp_err_t camera_service_set_enabled(bool enabled);
bool camera_service_is_enabled(void);
bool camera_service_is_ready(void);
const char *camera_service_sensor_name(void);
framesize_t camera_service_get_framesize(void);
camera_fb_t *camera_service_get_frame(void);
void camera_service_return_frame(camera_fb_t *frame);
esp_err_t camera_service_set_framesize(framesize_t framesize);
esp_err_t camera_service_begin_recording(void);
void camera_service_end_recording(void);
bool camera_service_claim_stream(void);
void camera_service_release_stream(void);
void camera_service_request_stream_stop(void);
bool camera_service_stream_stop_requested(void);
bool camera_service_wait_for_stream_idle(uint32_t timeout_ms);
