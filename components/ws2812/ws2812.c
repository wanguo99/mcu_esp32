#include "ws2812.h"
#include "ws2812_effects.h"
#include "peripheral_resources.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>

struct ws2812_instance {
    ws2812_config_t config;
    peripheral_claim_t claim;
    led_strip_handle_t driver;
    SemaphoreHandle_t lock, done;
    TaskHandle_t worker;
    bool closing;
    int64_t started_us;
    ws2812_status_t status;
};

static bool animated(ws2812_effect_t effect)
{ return effect == WS2812_EFFECT_BREATH || effect == WS2812_EFFECT_CHASE || effect == WS2812_EFFECT_BLINK; }

static esp_err_t render_locked(ws2812_handle_t strip)
{
    uint64_t elapsed = (uint64_t)(esp_timer_get_time() - strip->started_us);
    esp_err_t result = ESP_OK;
    for (uint32_t i = 0; i < strip->config.led_count; ++i) {
        ws2812_color_t color = ws2812_effect_pixel(&strip->status.effect, strip->config.led_count, i, elapsed);
        result = led_strip_set_pixel(strip->driver, i, color.red, color.green, color.blue);
        if (result != ESP_OK) break;
    }
    if (result == ESP_OK) result = led_strip_refresh(strip->driver);
    strip->status.last_error = result;
    if (result == ESP_OK) ++strip->status.frames_sent;
    return result;
}

static void animation_task(void *arg)
{
    ws2812_handle_t strip = arg;
    for (;;) {
        xSemaphoreTake(strip->lock, portMAX_DELAY);
        bool closing = strip->closing;
        TickType_t wait = animated(strip->status.effect.effect) ?
            pdMS_TO_TICKS(strip->config.refresh_interval_ms) : portMAX_DELAY;
        if (!wait) wait = 1;
        xSemaphoreGive(strip->lock);
        if (closing) break;
        ulTaskNotifyTake(pdTRUE, wait);
        xSemaphoreTake(strip->lock, portMAX_DELAY);
        closing = strip->closing;
        if (!closing && animated(strip->status.effect.effect)) render_locked(strip);
        xSemaphoreGive(strip->lock);
        if (closing) break;
    }
    // No access to the instance after publishing completion.
    xSemaphoreGive(strip->done);
    vTaskDelete(NULL);
}

esp_err_t ws2812_create(const ws2812_config_t *config, ws2812_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio) || !config->led_count ||
        config->led_count > 512 || (config->refresh_interval_ms &&
        (config->refresh_interval_ms < 10 || config->refresh_interval_ms > 1000)) ||
        (config->color_order != WS2812_ORDER_GRB && config->color_order != WS2812_ORDER_RGB))
        return ESP_ERR_INVALID_ARG;
    ws2812_handle_t strip = calloc(1, sizeof(*strip));
    if (!strip) return ESP_ERR_NO_MEM;
    strip->config = *config;
    if (!strip->config.refresh_interval_ms) strip->config.refresh_interval_ms = 20;
    strip->lock = xSemaphoreCreateMutex();
    strip->done = xSemaphoreCreateBinary();
    esp_err_t result = ESP_ERR_NO_MEM;
    if (!strip->lock || !strip->done) goto fail;
    strip->claim = (peripheral_claim_t){.gpios=UINT64_C(1) << config->gpio, .ledc_timer=-1, .ledc_channel=-1};
    result = peripheral_resources_acquire(&strip->claim);
    if (result != ESP_OK) goto fail;
    led_strip_config_t led = {.strip_gpio_num=config->gpio, .max_leds=config->led_count,
        .led_model=LED_MODEL_WS2812, .color_component_format=config->color_order == WS2812_ORDER_GRB ?
            LED_STRIP_COLOR_COMPONENT_FMT_GRB : LED_STRIP_COLOR_COMPONENT_FMT_RGB};
    led_strip_rmt_config_t rmt = {.resolution_hz=10000000, .flags.with_dma=config->with_dma};
    result = led_strip_new_rmt_device(&led, &rmt, &strip->driver);
    if (result != ESP_OK) goto release_claim;
    result = led_strip_clear(strip->driver);
    if (result == ESP_OK && xTaskCreate(animation_task, "ws2812", 3072, strip, 4, &strip->worker) != pdPASS)
        result = ESP_ERR_NO_MEM;
    if (result != ESP_OK) {
        led_strip_del(strip->driver);
        gpio_reset_pin(config->gpio);
        goto release_claim;
    }
    *out = strip;
    return ESP_OK;
release_claim:
    peripheral_resources_release(&strip->claim);
fail:
    if (strip->done) vSemaphoreDelete(strip->done);
    if (strip->lock) vSemaphoreDelete(strip->lock);
    free(strip);
    return result;
}

esp_err_t ws2812_set_effect(ws2812_handle_t strip, const ws2812_effect_config_t *effect)
{
    if (!strip || !effect || effect->effect < WS2812_EFFECT_OFF || effect->effect > WS2812_EFFECT_BLINK ||
        (animated(effect->effect) && (effect->period_ms < 100 || effect->period_ms > 60000)) ||
        effect->tail_length > strip->config.led_count) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(strip->lock, portMAX_DELAY);
    if (strip->closing) { xSemaphoreGive(strip->lock); return ESP_ERR_INVALID_STATE; }
    strip->status.effect = *effect;
    strip->started_us = esp_timer_get_time();
    esp_err_t result = render_locked(strip);
    xTaskNotifyGive(strip->worker);
    xSemaphoreGive(strip->lock);
    return result;
}

esp_err_t ws2812_set_color(ws2812_handle_t strip, ws2812_color_t color, uint8_t brightness)
{
    ws2812_effect_config_t effect = {.effect=WS2812_EFFECT_SOLID, .color=color, .brightness=brightness};
    return ws2812_set_effect(strip, &effect);
}

esp_err_t ws2812_stop(ws2812_handle_t strip)
{
    ws2812_effect_config_t effect = {.effect=WS2812_EFFECT_OFF};
    return ws2812_set_effect(strip, &effect);
}

esp_err_t ws2812_get_status(ws2812_handle_t strip, ws2812_status_t *out)
{
    if (!strip || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(strip->lock, portMAX_DELAY);
    *out = strip->status;
    xSemaphoreGive(strip->lock);
    return ESP_OK;
}

esp_err_t ws2812_destroy(ws2812_handle_t strip)
{
    if (!strip) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(strip->lock, portMAX_DELAY);
    strip->closing = true;
    if (strip->worker) xTaskNotifyGive(strip->worker);
    xSemaphoreGive(strip->lock);
    if (strip->worker) {
        xSemaphoreTake(strip->done, portMAX_DELAY);
        strip->worker = NULL;
    }
    esp_err_t result = led_strip_clear(strip->driver);
    if (result != ESP_OK) return result;
    result = led_strip_del(strip->driver);
    if (result != ESP_OK) return result;
    gpio_reset_pin(strip->config.gpio);
    peripheral_resources_release(&strip->claim);
    vSemaphoreDelete(strip->done);
    vSemaphoreDelete(strip->lock);
    free(strip);
    return ESP_OK;
}
