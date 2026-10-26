#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"

uint16_t app_settings_load_segment_minutes(void);
esp_err_t app_settings_save_segment_minutes(uint16_t minutes);
