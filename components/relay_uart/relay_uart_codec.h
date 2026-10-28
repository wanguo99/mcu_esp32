#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
void relay_modbus_request(uint8_t address, uint16_t coil, bool read, bool on, uint8_t out[8]);
esp_err_t relay_modbus_response(const uint8_t request[8], const uint8_t *response,
                                size_t length, bool *on);
