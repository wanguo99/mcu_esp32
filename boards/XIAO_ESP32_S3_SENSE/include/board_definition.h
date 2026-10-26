#pragma once
static const board_camera_pins_t board_camera = {
    .pwdn=-1, .reset=-1, .xclk=10, .sda=40, .scl=39,
    .d={15,17,18,16,14,12,11,48}, .vsync=38, .href=47, .pclk=13,
    .xclk_hz=20000000, .ledc_timer=0, .ledc_channel=0,
};
static const board_sd_pins_t board_sd = {.clk=7, .cmd=9, .d0=8};
static const board_config_t board_definition = {
    .name="XIAO_ESP32_S3_SENSE", .flash_mb=8, .psram_mb=8,
    .camera=&board_camera, .sd=&board_sd,
};
