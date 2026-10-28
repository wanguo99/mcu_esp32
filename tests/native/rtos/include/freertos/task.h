#pragma once
#include "FreeRTOS.h"
typedef struct native_task *TaskHandle_t;
int xTaskCreate(void (*function)(void *), const char *name, unsigned stack_size,
                void *arg, unsigned priority, TaskHandle_t *out);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(int clear, TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
