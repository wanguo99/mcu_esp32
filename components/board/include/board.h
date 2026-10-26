#pragma once
#include <stdint.h>
typedef struct {
    int pwdn, reset, xclk, sda, scl, d[8], vsync, href, pclk;
    uint32_t xclk_hz;
    int ledc_timer, ledc_channel;
} board_camera_pins_t;
typedef struct { int clk, cmd, d0; } board_sd_pins_t;
typedef struct {
    const char *name;
    unsigned flash_mb, psram_mb;
    const board_camera_pins_t *camera;
    const board_sd_pins_t *sd;
} board_config_t;
const board_config_t *board_get_config(void);
