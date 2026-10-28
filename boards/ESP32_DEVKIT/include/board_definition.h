#pragma once
// Generic original ESP32 core board. GPIO16/17 are conservatively left unused.
static const board_config_t board_definition = {
    .name="ESP32_DEVKIT", .flash_mb=4, .psram_mb=0,
    .camera=NULL, .sd=NULL,
};
