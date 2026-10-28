#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t console_parse_number(const char *text, double min, double max, double *out);
esp_err_t console_parse_hex(const char *text, uint8_t *out, size_t capacity, size_t *length);
