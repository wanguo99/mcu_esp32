#include "fan.h"
#include "peripheral_resources.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdlib.h>

#define FAN_COUNTER_LIMIT 30000
#define FAN_DUTY_MAX 1024U // 2^10 represents a constant active output on ESP32-S3.
struct fan_instance {
    fan_config_t config;
    peripheral_claim_t claim;
    SemaphoreHandle_t lock;
    pcnt_unit_handle_t counter;
    pcnt_channel_handle_t tach_channel;
    esp_timer_handle_t sampler;
    bool counter_enabled, counter_started, watch_added;
    int last_count;
    int64_t last_sample_us;
    fan_status_t status;
};

static void sample_rpm(void *arg)
{
    fan_handle_t fan = arg;
    // Never block the shared esp_timer task on an application mutex.
    if (xSemaphoreTake(fan->lock, 0) != pdTRUE) return;
    int count;
    int64_t now = esp_timer_get_time();
    if (pcnt_unit_get_count(fan->counter, &count) == ESP_OK && now > fan->last_sample_us) {
        uint32_t pulses = (uint32_t)count - (uint32_t)fan->last_count;
        fan->status.rpm = (float)((double)pulses * 60000000.0 /
            ((double)(now - fan->last_sample_us) * fan->config.pulses_per_revolution));
        fan->status.rpm_valid = true;
        fan->last_count = count;
        fan->last_sample_us = now;
    } else {
        fan->status.rpm_valid = false;
    }
    xSemaphoreGive(fan->lock);
}

static void release_tach(fan_handle_t fan)
{
    if (fan->counter_started) pcnt_unit_stop(fan->counter);
    if (fan->counter_enabled) pcnt_unit_disable(fan->counter);
    if (fan->tach_channel) pcnt_del_channel(fan->tach_channel);
    if (fan->watch_added) pcnt_unit_remove_watch_point(fan->counter, FAN_COUNTER_LIMIT);
    if (fan->counter) pcnt_del_unit(fan->counter);
}

static esp_err_t configure_tach(fan_handle_t fan)
{
    pcnt_unit_config_t unit = {.low_limit=-FAN_COUNTER_LIMIT, .high_limit=FAN_COUNTER_LIMIT,
                               .flags.accum_count=true};
    esp_err_t result = pcnt_new_unit(&unit, &fan->counter);
    if (result != ESP_OK) return result;
    pcnt_chan_config_t channel = {.edge_gpio_num=fan->config.tach_gpio, .level_gpio_num=-1};
    result = pcnt_new_channel(fan->counter, &channel, &fan->tach_channel);
    if (result != ESP_OK) return result;
    // Count one falling edge per tach pulse. External pull-up must be to 3.3 V.
    result = pcnt_channel_set_edge_action(fan->tach_channel, PCNT_CHANNEL_EDGE_ACTION_HOLD,
                                        PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    if (result != ESP_OK) return result;
    result = pcnt_channel_set_level_action(fan->tach_channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                         PCNT_CHANNEL_LEVEL_ACTION_KEEP);
    if (result != ESP_OK) return result;
    pcnt_glitch_filter_config_t filter = {.max_glitch_ns=1000};
    result = pcnt_unit_set_glitch_filter(fan->counter, &filter);
    if (result != ESP_OK) return result;
    result = pcnt_unit_add_watch_point(fan->counter, FAN_COUNTER_LIMIT);
    if (result != ESP_OK) return result;
    fan->watch_added = true;
    result = pcnt_unit_enable(fan->counter);
    if (result != ESP_OK) return result;
    fan->counter_enabled = true;
    result = pcnt_unit_clear_count(fan->counter);
    if (result != ESP_OK) return result;
    result = pcnt_unit_start(fan->counter);
    if (result != ESP_OK) return result;
    fan->counter_started = true;
    fan->last_sample_us = esp_timer_get_time();
    esp_timer_create_args_t timer = {.callback=sample_rpm, .arg=fan, .name="fan_rpm",
                                     .skip_unhandled_events=true};
    result = esp_timer_create(&timer, &fan->sampler);
    if (result != ESP_OK) return result;
    return esp_timer_start_periodic(fan->sampler, fan->config.sample_period_ms * 1000ULL);
}

esp_err_t fan_create(const fan_config_t *config, fan_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !GPIO_IS_VALID_OUTPUT_GPIO(config->pwm_gpio) || config->tach_gpio < -1 ||
        (config->tach_gpio >= 0 && (!GPIO_IS_VALID_GPIO(config->tach_gpio) ||
            config->tach_gpio == config->pwm_gpio || !config->pulses_per_revolution)) ||
        config->timer < 0 || config->timer >= LEDC_TIMER_MAX ||
        config->channel < 0 || config->channel >= LEDC_CHANNEL_MAX ||
        config->pwm_frequency_hz > 40000 ||
        (config->sample_period_ms && (config->sample_period_ms < 100 || config->sample_period_ms > 5000)))
        return ESP_ERR_INVALID_ARG;
    fan_handle_t fan = calloc(1, sizeof(*fan));
    if (!fan) return ESP_ERR_NO_MEM;
    fan->config = *config;
    if (!fan->config.pwm_frequency_hz) fan->config.pwm_frequency_hz = 25000;
    if (!fan->config.sample_period_ms) fan->config.sample_period_ms = 1000;
    fan->lock = xSemaphoreCreateMutex();
    if (!fan->lock) { free(fan); return ESP_ERR_NO_MEM; }
    fan->claim = (peripheral_claim_t){
        .gpios=UINT64_C(1) << config->pwm_gpio, .ledc_timer=config->timer,
        .ledc_channel=config->channel, .frequency_hz=fan->config.pwm_frequency_hz, .resolution_bits=10,
    };
    if (config->tach_gpio >= 0) fan->claim.gpios |= UINT64_C(1) << config->tach_gpio;
    esp_err_t result = peripheral_resources_acquire(&fan->claim);
    if (result != ESP_OK) goto fail;
    ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=config->timer,
        .duty_resolution=LEDC_TIMER_10_BIT, .freq_hz=fan->config.pwm_frequency_hz, .clk_cfg=LEDC_AUTO_CLK};
    result = ledc_timer_config(&timer);
    if (result == ESP_OK) {
        ledc_channel_config_t channel = {.gpio_num=config->pwm_gpio, .speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=config->channel, .timer_sel=config->timer, .duty=0,
            .flags.output_invert=config->pwm_inverted};
        result = ledc_channel_config(&channel);
    }
    if (result == ESP_OK && config->tach_gpio >= 0) {
        fan->status.tach_available = true;
        result = configure_tach(fan);
    }
    if (result != ESP_OK) {
        if (fan->sampler) { esp_timer_stop_blocking(fan->sampler, portMAX_DELAY); esp_timer_delete(fan->sampler); }
        release_tach(fan);
        ledc_stop(LEDC_LOW_SPEED_MODE, config->channel, 0);
        gpio_reset_pin(config->pwm_gpio);
        if (config->tach_gpio >= 0) gpio_reset_pin(config->tach_gpio);
        peripheral_resources_release(&fan->claim);
        goto fail;
    }
    *out = fan;
    return ESP_OK;
fail:
    vSemaphoreDelete(fan->lock);
    free(fan);
    return result;
}

esp_err_t fan_set_duty(fan_handle_t fan, unsigned percent)
{
    if (!fan || percent > 100) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(fan->lock, portMAX_DELAY);
    // The instance lock protects this exclusively owned channel; no fade service is needed.
    esp_err_t result = ledc_set_duty(LEDC_LOW_SPEED_MODE, fan->config.channel,
                                    (percent * FAN_DUTY_MAX + 50) / 100);
    if (result == ESP_OK) result = ledc_update_duty(LEDC_LOW_SPEED_MODE, fan->config.channel);
    if (result == ESP_OK) fan->status.duty_percent = percent;
    xSemaphoreGive(fan->lock);
    return result;
}

esp_err_t fan_stop(fan_handle_t fan) { return fan_set_duty(fan, 0); }

esp_err_t fan_get_status(fan_handle_t fan, fan_status_t *out)
{
    if (!fan || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(fan->lock, portMAX_DELAY);
    *out = fan->status;
    int64_t age = (esp_timer_get_time() - fan->last_sample_us) / 1000;
    out->sample_age_ms = !out->tach_available ? 0 : (age > UINT32_MAX ? UINT32_MAX : (uint32_t)age);
    if (age > (int64_t)fan->config.sample_period_ms * 3) out->rpm_valid = false;
    xSemaphoreGive(fan->lock);
    return ESP_OK;
}

esp_err_t fan_destroy(fan_handle_t fan)
{
    if (!fan) return ESP_ERR_INVALID_ARG;
    esp_err_t result = fan_stop(fan);
    if (result != ESP_OK) return result;
    if (fan->sampler) {
        result = esp_timer_stop_blocking(fan->sampler, portMAX_DELAY);
        if (result != ESP_OK) return result;
        result = esp_timer_delete(fan->sampler);
        if (result != ESP_OK) return result;
    }
    release_tach(fan);
    gpio_reset_pin(fan->config.pwm_gpio);
    if (fan->config.tach_gpio >= 0) gpio_reset_pin(fan->config.tach_gpio);
    peripheral_resources_release(&fan->claim);
    vSemaphoreDelete(fan->lock);
    free(fan);
    return ESP_OK;
}
