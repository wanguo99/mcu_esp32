#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_err.h"
typedef enum { FRAMESIZE_QQVGA, FRAMESIZE_SVGA=11, FRAMESIZE_UXGA=15, FRAMESIZE_INVALID } framesize_t;
typedef struct { int id; } camera_fb_t;
typedef struct {
    int pin_pwdn, pin_reset, pin_xclk, pin_sccb_sda, pin_sccb_scl;
    int pin_d0, pin_d1, pin_d2, pin_d3, pin_d4, pin_d5, pin_d6, pin_d7;
    int pin_vsync, pin_href, pin_pclk, xclk_freq_hz, ledc_timer, ledc_channel;
    int pixel_format, jpeg_quality, fb_count, fb_location, grab_mode;
    framesize_t frame_size;
} camera_config_t;
typedef struct sensor sensor_t;
struct sensor {
    struct { unsigned PID; } id;
    int (*set_vflip)(sensor_t *, int);
    int (*set_brightness)(sensor_t *, int);
    int (*set_saturation)(sensor_t *, int);
    int (*set_framesize)(sensor_t *, framesize_t);
    int (*set_reg)(sensor_t *, int, int, int);
};
#define OV3660_PID 0x3660
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define PIXFORMAT_JPEG 0
#define CAMERA_FB_IN_PSRAM 0
#define CAMERA_GRAB_WHEN_EMPTY 0
esp_err_t esp_camera_init(const camera_config_t *config);
esp_err_t esp_camera_deinit(void);
sensor_t *esp_camera_sensor_get(void);
camera_fb_t *esp_camera_fb_get(void);
void esp_camera_fb_return(camera_fb_t *frame);
size_t strlcpy(char *dest, const char *src, size_t size);
