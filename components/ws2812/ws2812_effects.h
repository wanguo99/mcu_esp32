#pragma once
#include "ws2812.h"
ws2812_color_t ws2812_effect_pixel(const ws2812_effect_config_t *effect, uint32_t count,
                                  uint32_t index, uint64_t elapsed_us);
