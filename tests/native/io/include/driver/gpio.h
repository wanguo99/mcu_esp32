#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#if CONFIG_IDF_TARGET_ESP32
#define SOC_GPIO_VALID_GPIO_MASK (0xFFFFFFFFFFULL & ~((1ULL << 24) | (0xFULL << 28)))
#else
#define SOC_GPIO_VALID_GPIO_MASK (0x1FFFFFFFFFFFFULL & ~(0xFULL << 22))
#endif
#define SOC_HP_I2C_NUM 2U
#define GPIO_IS_VALID_GPIO(gpio) ((gpio) >= 0 && (gpio) < 49 && (SOC_GPIO_VALID_GPIO_MASK & (1ULL << (gpio))))
#if CONFIG_IDF_TARGET_ESP32
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) (GPIO_IS_VALID_GPIO(gpio) && (gpio) < 34)
#else
#define GPIO_IS_VALID_OUTPUT_GPIO(gpio) GPIO_IS_VALID_GPIO(gpio)
#endif
esp_err_t gpio_reset_pin(int gpio);
typedef enum { GPIO_PULLUP_ONLY, GPIO_PULLDOWN_ONLY, GPIO_PULLUP_PULLDOWN, GPIO_FLOATING } gpio_pull_mode_t;
esp_err_t gpio_set_level(int gpio, uint32_t level);
esp_err_t gpio_set_pull_mode(int gpio, gpio_pull_mode_t mode);
esp_err_t gpio_input_enable(int gpio);
