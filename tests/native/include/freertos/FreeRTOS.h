#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
