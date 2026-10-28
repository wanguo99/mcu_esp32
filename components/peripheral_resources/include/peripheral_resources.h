#pragma once
#include <stdint.h>
#include "esp_err.h"

// Internal resource claims for reusable peripheral drivers. Use -1 for no LEDC.
typedef struct {
    uint64_t gpios;
    int ledc_timer, ledc_channel;
    uint32_t frequency_hz;
    unsigned resolution_bits;
    uint32_t i2c_ports; // Bit mask of exclusively owned I2C controllers; 0 for none.
} peripheral_claim_t;

esp_err_t peripheral_resources_acquire(const peripheral_claim_t *claim);
// Release exactly the claim acquired successfully, once, after stopping hardware.
void peripheral_resources_release(const peripheral_claim_t *claim);
