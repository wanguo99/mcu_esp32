#pragma once
#include <stdatomic.h>
#include "ws2812.h"
extern atomic_int test_active_strips, test_active_tasks;
extern atomic_bool test_fail_refresh, test_fail_task, test_fail_device;
ws2812_color_t test_led_pixel(unsigned index); // Use when no animation is running.
