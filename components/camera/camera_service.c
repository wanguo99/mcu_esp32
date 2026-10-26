#include "camera_service.h"

#include <stdatomic.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "app_config.h"
#include "board.h"

#define OV3660_SYSTEM_CONTROL0 0x3008
#define OV3660_SOFTWARE_POWER_DOWN 0x40

static const char *TAG = "camera_service";

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_control_lock;
static SemaphoreHandle_t s_frame_lock;
static atomic_bool s_ready;
static atomic_int s_framesize = FRAMESIZE_INVALID;
static bool s_recording;
static bool s_stream_claimed;
static bool s_stream_stop_requested;
// Includes both borrowed frames and callers waiting inside esp_camera_fb_get().
// Never hold s_frame_lock while waiting for a frame: returners must be able to run.
static uint32_t s_frame_users;
static bool s_frames_paused;
static char s_sensor_name[24] = "unavailable";

static camera_config_t s_camera_config = {
    .pin_pwdn = -1,
    .pin_reset = -1,
    .pin_xclk = -1,
    .pin_sccb_sda = -1,
    .pin_sccb_scl = -1,
    .pin_d0 = -1,
    .pin_d1 = -1,
    .pin_d2 = -1,
    .pin_d3 = -1,
    .pin_d4 = -1,
    .pin_d5 = -1,
    .pin_d6 = -1,
    .pin_d7 = -1,
    .pin_vsync = -1,
    .pin_href = -1,
    .pin_pclk = -1,
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_UXGA,
    .jpeg_quality = 10,
    .fb_count = 2,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

esp_err_t camera_service_init(void)
{
    if (s_control_lock) return ESP_OK;
    const board_camera_pins_t *pins = board_get_config()->camera;
    if (!pins) return ESP_ERR_NOT_SUPPORTED;
    s_lock = xSemaphoreCreateMutex();
    s_frame_lock = xSemaphoreCreateMutex();
    s_control_lock = xSemaphoreCreateMutex();
    if (!s_lock || !s_frame_lock || !s_control_lock) {
        if (s_lock) vSemaphoreDelete(s_lock);
        if (s_frame_lock) vSemaphoreDelete(s_frame_lock);
        if (s_control_lock) vSemaphoreDelete(s_control_lock);
        s_lock = s_frame_lock = s_control_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    s_camera_config.pin_pwdn = pins->pwdn;
    s_camera_config.pin_reset = pins->reset;
    s_camera_config.pin_xclk = pins->xclk;
    s_camera_config.pin_sccb_sda = pins->sda;
    s_camera_config.pin_sccb_scl = pins->scl;
    s_camera_config.pin_d0 = pins->d[0];
    s_camera_config.pin_d1 = pins->d[1];
    s_camera_config.pin_d2 = pins->d[2];
    s_camera_config.pin_d3 = pins->d[3];
    s_camera_config.pin_d4 = pins->d[4];
    s_camera_config.pin_d5 = pins->d[5];
    s_camera_config.pin_d6 = pins->d[6];
    s_camera_config.pin_d7 = pins->d[7];
    s_camera_config.pin_vsync = pins->vsync;
    s_camera_config.pin_href = pins->href;
    s_camera_config.pin_pclk = pins->pclk;
    s_camera_config.xclk_freq_hz = pins->xclk_hz;
    s_camera_config.ledc_timer = pins->ledc_timer;
    s_camera_config.ledc_channel = pins->ledc_channel;
    // Camera hardware is intentionally off at boot. It is initialized only
    // after the user enables it from the web console.
    atomic_store(&s_ready, false);
    s_recording = false;
    s_stream_claimed = false;
    s_stream_stop_requested = false;
    s_frame_users = 0;
    s_frames_paused = false;
    strlcpy(s_sensor_name, "disabled", sizeof(s_sensor_name));
    ESP_LOGI(TAG, "Camera service ready; camera is disabled until explicitly enabled");
    return ESP_OK;
}

static esp_err_t init_camera_locked(void)
{
    if (atomic_load(&s_ready)) {
        return ESP_OK;
    }

    esp_err_t result = esp_camera_init(&s_camera_config);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed: %s", esp_err_to_name(result));
        return result;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) {
        esp_camera_deinit();
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(s_sensor_name, sizeof(s_sensor_name), "PID-0x%04X", sensor->id.PID);
    if (sensor->id.PID == OV3660_PID) {
        strlcpy(s_sensor_name, "OV3660", sizeof(s_sensor_name));
        sensor->set_vflip(sensor, 1);
        sensor->set_brightness(sensor, 1);
        sensor->set_saturation(sensor, -2);
    }
    // Keep UXGA as the allocation ceiling, but start at SVGA to leave more
    // capture and SD bandwidth for the live stream and recorder.
    if (sensor->set_framesize(sensor, FRAMESIZE_SVGA) != 0) {
        esp_camera_deinit();
        return ESP_FAIL;
    }

    atomic_store(&s_framesize, FRAMESIZE_SVGA);
    atomic_store(&s_ready, true);
    ESP_LOGI(TAG, "Camera ready: %s", s_sensor_name);
    return ESP_OK;
}

static esp_err_t set_enabled_serialized(bool enabled)
{
    if (!s_lock || !s_frame_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    if (enabled) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        esp_err_t result = init_camera_locked();
        xSemaphoreGive(s_frame_lock);
        xSemaphoreGive(s_lock);
        return result;
    }

    // Mark the camera unavailable first. An active stream then exits its
    // capture loop and releases its claim before the driver is deinitialized.
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!atomic_load(&s_ready)) {
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }
    if (s_recording) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    atomic_store(&s_ready, false);
    xSemaphoreGive(s_lock);

    for (int attempt = 0; attempt < 240; ++attempt) {
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        uint32_t frame_users = s_frame_users;
        xSemaphoreGive(s_frame_lock);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool stream_claimed = s_stream_claimed;
        xSemaphoreGive(s_lock);
        if (frame_users == 0 && !stream_claimed) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    sensor_t *sensor = esp_camera_sensor_get();
    if (s_frame_users == 0 && !s_stream_claimed && sensor && sensor->id.PID == OV3660_PID) {
        // Put OV3660 into software power-down before SCCB and XCLK are released.
        if (sensor->set_reg(sensor, OV3660_SYSTEM_CONTROL0, OV3660_SOFTWARE_POWER_DOWN,
                            OV3660_SOFTWARE_POWER_DOWN) != 0) {
            ESP_LOGW(TAG, "Unable to put OV3660 into software power-down");
        }
    }
    esp_err_t result =
        (s_frame_users == 0 && !s_stream_claimed) ? esp_camera_deinit() : ESP_ERR_TIMEOUT;
    if (result == ESP_OK) {
        atomic_store(&s_framesize, FRAMESIZE_INVALID);
        strlcpy(s_sensor_name, "disabled", sizeof(s_sensor_name));
        ESP_LOGI(TAG, "Camera disabled and hardware released");
    } else if (result == ESP_ERR_TIMEOUT) {
        // The driver is still initialized, so keep the service usable and let
        // the caller retry the shutdown after the active user exits.
        atomic_store(&s_ready, true);
        ESP_LOGW(TAG, "Timed out waiting for camera users to stop");
    } else {
        atomic_store(&s_ready, true);
    }
    xSemaphoreGive(s_frame_lock);
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t camera_service_set_enabled(bool enabled)
{
    if (!s_control_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    esp_err_t result = set_enabled_serialized(enabled);
    xSemaphoreGive(s_control_lock);
    return result;
}

bool camera_service_is_enabled(void)
{
    return camera_service_is_ready();
}

bool camera_service_is_ready(void)
{
    return atomic_load(&s_ready);
}

const char *camera_service_sensor_name(void)
{
    return s_sensor_name;
}

framesize_t camera_service_get_framesize(void)
{
    return atomic_load(&s_ready) ? atomic_load(&s_framesize) : FRAMESIZE_INVALID;
}

camera_fb_t *camera_service_get_frame(void)
{
    if (!s_lock || !s_frame_lock || xSemaphoreTake(s_frame_lock, portMAX_DELAY) != pdTRUE) {
        return NULL;
    }
    if (!atomic_load(&s_ready) || s_frames_paused) {
        xSemaphoreGive(s_frame_lock);
        return NULL;
    }
    // Reserve the driver lifetime before releasing the mutex, including when
    // the driver's frame queue is empty. Shutdown waits for this reservation.
    ++s_frame_users;
    xSemaphoreGive(s_frame_lock);
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) {
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        --s_frame_users;
        xSemaphoreGive(s_frame_lock);
    }
    return frame;
}

void camera_service_return_frame(camera_fb_t *frame)
{
    if (frame) {
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        esp_camera_fb_return(frame);
        if (s_frame_users > 0) {
            --s_frame_users;
        }
        xSemaphoreGive(s_frame_lock);
    }
}

static esp_err_t set_framesize_serialized(framesize_t framesize)
{
    if (!s_lock || !s_frame_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (framesize < FRAMESIZE_QQVGA || framesize > FRAMESIZE_UXGA) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!atomic_load(&s_ready) || s_recording) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    s_frames_paused = true;
    s_stream_stop_requested = s_stream_claimed;
    xSemaphoreGive(s_frame_lock);
    xSemaphoreGive(s_lock);

    if (!camera_service_wait_for_stream_idle(6000)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        s_frames_paused = false;
        s_stream_stop_requested = false;
        xSemaphoreGive(s_frame_lock);
        xSemaphoreGive(s_lock);
        return ESP_ERR_TIMEOUT;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    sensor_t *sensor = esp_camera_sensor_get();
    if (!atomic_load(&s_ready) || !sensor || sensor->set_framesize(sensor, framesize) != 0) {
        result = ESP_FAIL;
    } else {
        atomic_store(&s_framesize, framesize);
    }
    s_frames_paused = false;
    s_stream_stop_requested = false;
    xSemaphoreGive(s_frame_lock);
    xSemaphoreGive(s_lock);
    return result;
}

esp_err_t camera_service_set_framesize(framesize_t framesize)
{
    if (!s_control_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_control_lock, portMAX_DELAY);
    esp_err_t result = set_framesize_serialized(framesize);
    xSemaphoreGive(s_control_lock);
    return result;
}

esp_err_t camera_service_begin_recording(void)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    esp_err_t result = !atomic_load(&s_ready) || s_frames_paused || s_recording
                           ? ESP_ERR_INVALID_STATE : ESP_OK;
    if (result == ESP_OK) {
        s_recording = true;
    }
    xSemaphoreGive(s_frame_lock);
    xSemaphoreGive(s_lock);
    return result;
}

void camera_service_end_recording(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_recording = false;
    xSemaphoreGive(s_lock);
}

bool camera_service_claim_stream(void)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    bool claimed = atomic_load(&s_ready) && !s_frames_paused && !s_stream_claimed;
    if (claimed) {
        s_stream_claimed = true;
        s_stream_stop_requested = false;
    }
    xSemaphoreGive(s_frame_lock);
    xSemaphoreGive(s_lock);
    return claimed;
}

void camera_service_release_stream(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream_claimed = false;
    s_stream_stop_requested = false;
    xSemaphoreGive(s_lock);
}

void camera_service_request_stream_stop(void)
{
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_stream_stop_requested = true;
    xSemaphoreGive(s_lock);
}

bool camera_service_stream_stop_requested(void)
{
    if (!s_lock) {
        return true;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool requested = s_stream_stop_requested;
    xSemaphoreGive(s_lock);
    return requested;
}

bool camera_service_wait_for_stream_idle(uint32_t timeout_ms)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        bool stream_claimed = s_stream_claimed;
        xSemaphoreGive(s_lock);
        xSemaphoreTake(s_frame_lock, portMAX_DELAY);
        bool frame_users = s_frame_users != 0;
        xSemaphoreGive(s_frame_lock);
        if (!stream_claimed && !frame_users) {
            return true;
        }
        if ((int32_t)(xTaskGetTickCount() - deadline) >= 0) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
