#pragma once
#include <stdint.h>
#include <pthread.h>
typedef uint32_t TickType_t;
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(lock) pthread_mutex_lock(lock)
#define portEXIT_CRITICAL(lock) pthread_mutex_unlock(lock)
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdPASS 1
void vTaskDelay(TickType_t ticks);
