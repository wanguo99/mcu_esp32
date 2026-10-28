#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct test_led_strip *led_strip_handle_t;
typedef struct { unsigned components; } test_color_format_t;
#define LED_MODEL_WS2812 0
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB ((test_color_format_t){.components=0})
#define LED_STRIP_COLOR_COMPONENT_FMT_RGB ((test_color_format_t){.components=1})
typedef struct {
    int strip_gpio_num, led_model;
    uint32_t max_leds;
    test_color_format_t color_component_format;
} led_strip_config_t;
typedef struct { uint32_t resolution_hz; struct { bool with_dma; } flags; } led_strip_rmt_config_t;
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *, const led_strip_rmt_config_t *, led_strip_handle_t *);
esp_err_t led_strip_clear(led_strip_handle_t);
esp_err_t led_strip_set_pixel(led_strip_handle_t, uint32_t, uint32_t, uint32_t, uint32_t);
esp_err_t led_strip_refresh(led_strip_handle_t);
esp_err_t led_strip_del(led_strip_handle_t);
