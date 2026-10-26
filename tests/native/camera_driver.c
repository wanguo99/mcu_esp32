// Host hardware substitute. Tests compile the actual camera_service.c against
// a two-buffer camera queue and pthread-backed FreeRTOS synchronization.
#include "esp_camera.h"
#include "freertos/semphr.h"
#include "board.h"
#include "board_definition.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

struct native_mutex { pthread_mutex_t mutex; };
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t handle = malloc(sizeof(*handle));
    if (handle) pthread_mutex_init(&handle->mutex, NULL);
    return handle;
}
int xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks)
{
    return (ticks ? pthread_mutex_lock(&handle->mutex) : pthread_mutex_trylock(&handle->mutex)) == 0;
}
void xSemaphoreGive(SemaphoreHandle_t handle) { pthread_mutex_unlock(&handle->mutex); }
void vSemaphoreDelete(SemaphoreHandle_t handle)
{ pthread_mutex_destroy(&handle->mutex); free(handle); }
TickType_t xTaskGetTickCount(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (TickType_t)(now.tv_sec * 1000ULL + now.tv_nsec / 1000000);
}
void vTaskDelay(TickType_t ticks)
{
    struct timespec wait = {.tv_sec=ticks/1000, .tv_nsec=(ticks%1000)*1000000};
    nanosleep(&wait, NULL);
}
const board_config_t *board_get_config(void) { return &board_definition; }
size_t strlcpy(char *dest, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) { size_t copied = len < size-1 ? len : size-1; memcpy(dest, src, copied); dest[copied]=0; }
    return len;
}

static pthread_mutex_t s_driver_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_frame_ready = PTHREAD_COND_INITIALIZER;
static camera_fb_t s_frames[2] = {{0}, {1}};
static bool s_borrowed[2], s_initialized;
static atomic_int s_waiters, s_bad_deinit, s_fail_next;
static int sensor_option(sensor_t *sensor, int value) { (void)sensor; (void)value; return 0; }
static int sensor_size(sensor_t *sensor, framesize_t value) { (void)sensor; (void)value; return 0; }
static int sensor_reg(sensor_t *sensor, int reg, int mask, int value)
{ (void)sensor; (void)reg; (void)mask; (void)value; return 0; }
static sensor_t s_sensor = {
    .id={OV3660_PID}, .set_vflip=sensor_option, .set_brightness=sensor_option,
    .set_saturation=sensor_option, .set_framesize=sensor_size, .set_reg=sensor_reg,
};
esp_err_t esp_camera_init(const camera_config_t *config)
{
    (void)config;
    pthread_mutex_lock(&s_driver_lock);
    s_initialized = true;
    s_borrowed[0] = s_borrowed[1] = false;
    pthread_mutex_unlock(&s_driver_lock);
    return ESP_OK;
}
esp_err_t esp_camera_deinit(void)
{
    pthread_mutex_lock(&s_driver_lock);
    bool busy = s_borrowed[0] || s_borrowed[1] || atomic_load(&s_waiters);
    if (busy) atomic_fetch_add(&s_bad_deinit, 1);
    else s_initialized = false;
    pthread_mutex_unlock(&s_driver_lock);
    return busy ? ESP_FAIL : ESP_OK;
}
sensor_t *esp_camera_sensor_get(void) { return s_initialized ? &s_sensor : NULL; }
camera_fb_t *esp_camera_fb_get(void)
{
    if (atomic_exchange(&s_fail_next, 0)) return NULL;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    ++deadline.tv_sec;
    pthread_mutex_lock(&s_driver_lock);
    while (s_initialized) {
        for (int i = 0; i < 2; ++i) {
            if (!s_borrowed[i]) {
                s_borrowed[i] = true;
                pthread_mutex_unlock(&s_driver_lock);
                return &s_frames[i];
            }
        }
        atomic_fetch_add(&s_waiters, 1);
        int result = pthread_cond_timedwait(&s_frame_ready, &s_driver_lock, &deadline);
        atomic_fetch_sub(&s_waiters, 1);
        if (result == ETIMEDOUT) break;
    }
    pthread_mutex_unlock(&s_driver_lock);
    return NULL;
}
void esp_camera_fb_return(camera_fb_t *frame)
{
    pthread_mutex_lock(&s_driver_lock);
    s_borrowed[frame->id] = false;
    pthread_cond_signal(&s_frame_ready);
    pthread_mutex_unlock(&s_driver_lock);
}
int test_camera_waiters(void) { return atomic_load(&s_waiters); }
int test_camera_bad_deinit(void) { return atomic_load(&s_bad_deinit); }
int test_camera_initialized(void) { return s_initialized; }
void test_camera_fail_next(void) { atomic_store(&s_fail_next, 1); }
