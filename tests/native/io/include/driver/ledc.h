#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define LEDC_TIMER_MAX 4
#define LEDC_CHANNEL_MAX 8
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_TIMER_14_BIT 14
#define LEDC_AUTO_CLK 0
typedef struct { int speed_mode, timer_num, duty_resolution; uint32_t freq_hz; int clk_cfg; } ledc_timer_config_t;
typedef struct {
    int gpio_num, speed_mode, channel, timer_sel;
    uint32_t duty, hpoint;
    struct { unsigned output_invert:1; } flags;
} ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *config);
esp_err_t ledc_channel_config(const ledc_channel_config_t *config);
esp_err_t ledc_set_duty_and_update(int mode, int channel, uint32_t duty, uint32_t hpoint);
esp_err_t ledc_set_duty(int mode, int channel, uint32_t duty);
esp_err_t ledc_update_duty(int mode, int channel);
esp_err_t ledc_stop(int mode, int channel, uint32_t idle);
uint32_t ledc_get_freq(int mode,int timer);
uint32_t ledc_get_duty(int mode,int channel);
