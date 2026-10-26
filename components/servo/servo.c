#include "servo.h"
#include "board.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdlib.h>

struct servo_instance { servo_config_t config; SemaphoreHandle_t lock; };
static portMUX_TYPE s_resource_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_channels, s_timer_users[LEDC_TIMER_MAX], s_timer_hz[LEDC_TIMER_MAX];
static int s_channel_gpio[LEDC_CHANNEL_MAX];

static bool conflicts_with_board(const servo_config_t *config)
{
    const board_config_t *board = board_get_config();
    if (config->gpio == 19 || config->gpio == 20 ||
        (board->psram_mb == 8 && config->gpio >= 35 && config->gpio <= 37)) return true;
#if CONFIG_MCU_ENABLE_CAMERA
    const board_camera_pins_t *camera = board->camera;
    if (camera) {
        if (config->timer == camera->ledc_timer || config->channel == camera->ledc_channel ||
            config->gpio == camera->xclk || config->gpio == camera->sda || config->gpio == camera->scl ||
            config->gpio == camera->vsync || config->gpio == camera->href || config->gpio == camera->pclk)
            return true;
        for (int i = 0; i < 8; ++i) if (config->gpio == camera->d[i]) return true;
    }
#endif
#if CONFIG_MCU_ENABLE_STORAGE
    if (board->sd && (config->gpio == board->sd->clk || config->gpio == board->sd->cmd ||
                      config->gpio == board->sd->d0)) return true;
#endif
    return false;
}

static void release_resources(const servo_config_t *config)
{
    portENTER_CRITICAL(&s_resource_lock);
    s_channels &= ~(1U << config->channel);
    --s_timer_users[config->timer];
    portEXIT_CRITICAL(&s_resource_lock);
}

esp_err_t servo_create(const servo_config_t *config, servo_handle_t *out)
{
    if (!config || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->gpio) || config->timer < 0 || config->timer >= LEDC_TIMER_MAX ||
        config->channel < 0 || config->channel >= LEDC_CHANNEL_MAX || !config->frequency_hz ||
        config->frequency_hz > 1000 || !config->min_pulse_us || config->min_pulse_us >= config->max_pulse_us ||
        (uint64_t)config->max_pulse_us * config->frequency_hz >= 1000000 ||
        !isfinite(config->min_angle) || !isfinite(config->max_angle) || config->min_angle >= config->max_angle ||
        conflicts_with_board(config)) return ESP_ERR_INVALID_ARG;
    servo_handle_t servo = calloc(1, sizeof(*servo));
    if (!servo) return ESP_ERR_NO_MEM;
    servo->lock = xSemaphoreCreateMutex();
    if (!servo->lock) { free(servo); return ESP_ERR_NO_MEM; }
    servo->config = *config;
    portENTER_CRITICAL(&s_resource_lock);
    bool busy = (s_channels & (1U << config->channel)) ||
        (s_timer_users[config->timer] && s_timer_hz[config->timer] != config->frequency_hz);
    for (int channel = 0; channel < LEDC_CHANNEL_MAX; ++channel)
        if ((s_channels & (1U << channel)) && s_channel_gpio[channel] == config->gpio) busy = true;
    if (!busy) {
        s_channels |= 1U << config->channel;
        s_channel_gpio[config->channel] = config->gpio;
        ++s_timer_users[config->timer];
        s_timer_hz[config->timer] = config->frequency_hz;
    }
    portEXIT_CRITICAL(&s_resource_lock);
    if (busy) { vSemaphoreDelete(servo->lock); free(servo); return ESP_ERR_INVALID_STATE; }
    ledc_timer_config_t timer = {
        .speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=config->timer,
        .duty_resolution=LEDC_TIMER_14_BIT, .freq_hz=config->frequency_hz, .clk_cfg=LEDC_AUTO_CLK,
    };
    esp_err_t result = ledc_timer_config(&timer);
    if (result == ESP_OK) {
        ledc_channel_config_t channel = {
            .gpio_num=config->gpio, .speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=config->channel, .timer_sel=config->timer, .duty=0, .hpoint=0,
        };
        result = ledc_channel_config(&channel);
    }
    if (result != ESP_OK) {
        release_resources(config);
        vSemaphoreDelete(servo->lock); free(servo);
        return result;
    }
    *out = servo;
    return ESP_OK;
}

esp_err_t servo_set_angle(servo_handle_t servo, float angle)
{
    if (!servo || !isfinite(angle) || angle < servo->config.min_angle || angle > servo->config.max_angle)
        return ESP_ERR_INVALID_ARG;
    const servo_config_t *config = &servo->config;
    float pulse = config->min_pulse_us + (angle-config->min_angle) *
        (config->max_pulse_us-config->min_pulse_us)/(config->max_angle-config->min_angle);
    uint32_t duty = (uint32_t)(pulse * config->frequency_hz * 16384.0f / 1000000.0f);
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    esp_err_t result = ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, config->channel, duty, 0);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_stop(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    esp_err_t result = ledc_stop(LEDC_LOW_SPEED_MODE, servo->config.channel, 0);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_destroy(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    esp_err_t result = servo_stop(servo);
    if (result != ESP_OK) return result;
    gpio_reset_pin(servo->config.gpio);
    release_resources(&servo->config);
    vSemaphoreDelete(servo->lock); free(servo);
    return ESP_OK;
}
