#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "app_config.h"

typedef struct {
    bool ap_active;
    char ssid[33];
    char ip[16];
} wifi_manager_status_t;

esp_err_t wifi_manager_init(void);
void wifi_manager_get_status(wifi_manager_status_t *status);

// Optional station link; AP remains available. No scan or UI dependency.
typedef struct {
    bool connected;
    int rssi_dbm;
    uint8_t bssid[6];
    uint8_t channel;
    int64_t timestamp_us;
} wifi_signal_sample_t;
esp_err_t wifi_manager_get_signal(wifi_signal_sample_t *sample);
