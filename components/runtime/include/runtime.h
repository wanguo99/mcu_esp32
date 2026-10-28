#pragma once
#include "esp_err.h"
esp_err_t runtime_start(void);
esp_err_t runtime_prepare_restart(void);
typedef esp_err_t (*runtime_shutdown_hook_t)(void);
// Application-owned peripherals can stop before recording/camera shutdown.
esp_err_t runtime_register_shutdown_hook(runtime_shutdown_hook_t hook);
