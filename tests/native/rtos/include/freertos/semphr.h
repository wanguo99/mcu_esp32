#pragma once
#include "FreeRTOS.h"
typedef struct native_semaphore *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks);
void xSemaphoreGive(SemaphoreHandle_t handle);
void vSemaphoreDelete(SemaphoreHandle_t handle);

typedef struct { int placeholder; } StaticSemaphore_t;
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buffer);
