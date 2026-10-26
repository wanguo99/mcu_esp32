#pragma once
#include "FreeRTOS.h"
typedef struct native_mutex *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks);
void xSemaphoreGive(SemaphoreHandle_t handle);
void vSemaphoreDelete(SemaphoreHandle_t handle);
