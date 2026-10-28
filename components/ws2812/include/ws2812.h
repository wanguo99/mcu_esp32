#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct ws2812_instance *ws2812_handle_t;
typedef struct { uint8_t red, green, blue; } ws2812_color_t;
typedef enum { WS2812_ORDER_GRB, WS2812_ORDER_RGB } ws2812_color_order_t;
typedef enum {
    WS2812_EFFECT_OFF, WS2812_EFFECT_SOLID, WS2812_EFFECT_BREATH,
    WS2812_EFFECT_CHASE, WS2812_EFFECT_BLINK,
} ws2812_effect_t;
typedef struct {
    int gpio;
    uint32_t led_count; // 1..512; RMT channel allocated automatically by the SDK.
    uint32_t refresh_interval_ms; // 0 selects 20 ms; otherwise 10..1000 ms.
    bool with_dma;
    ws2812_color_order_t color_order;
} ws2812_config_t;
#define WS2812_CONFIG_DEFAULT(data_pin, count) { \
    .gpio=(data_pin), .led_count=(count), .refresh_interval_ms=20, \
    .with_dma=true, .color_order=WS2812_ORDER_GRB \
}
typedef struct {
    ws2812_effect_t effect;
    ws2812_color_t color;
    uint8_t brightness; // 0..255, scales all color components.
    uint32_t period_ms; // Animated effects: 100..60000 ms per full cycle/traversal.
    uint32_t tail_length; // CHASE: 0 selects 1; otherwise <= led_count.
    bool reverse;
} ws2812_effect_config_t;
#define WS2812_EFFECT_DEFAULT(effect_kind) { \
    .effect=(effect_kind), .color={255,255,255}, .brightness=32, .period_ms=2000, .tail_length=1 \
}
typedef struct {
    ws2812_effect_config_t effect;
    esp_err_t last_error;
    uint64_t frames_sent;
} ws2812_status_t;

// Creates an initially dark strip and its background animation task.
esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out);
// Applies the first frame synchronously, then animates in the background.
esp_err_t ws2812_set_effect(ws2812_handle_t strip, const ws2812_effect_config_t *effect);
esp_err_t ws2812_set_color(ws2812_handle_t strip, ws2812_color_t color, uint8_t brightness);
esp_err_t ws2812_stop(ws2812_handle_t strip); // Stops animations and turns LEDs off.
esp_err_t ws2812_get_status(ws2812_handle_t strip, ws2812_status_t *out);
// Waits for the worker, turns off LEDs and releases resources. Task context only;
// caller must exclude other users while destroying. Failed teardown may be retried.
esp_err_t ws2812_destroy(ws2812_handle_t strip);
