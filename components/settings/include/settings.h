#pragma once
#include <stdint.h>
#include "esp_err.h"

// NVS initialization never erases existing application data on errors.
esp_err_t settings_init(void);
uint16_t settings_get_u16(const char *name_space, const char *key, uint16_t fallback);
esp_err_t settings_set_u16(const char *name_space, const char *key, uint16_t value);
