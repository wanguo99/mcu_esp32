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
