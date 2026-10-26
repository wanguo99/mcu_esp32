#include "monitor_web.h"
#include "web_server.h"
#include "app_config.h"
#include "camera_service.h"
#include "wifi_manager.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdatomic.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#if CONFIG_MCU_ENABLE_STORAGE
#include "storage_service.h"
#endif
#if CONFIG_MCU_ENABLE_RECORDING
#include "recording_service.h"
#endif
#define set_common_headers web_set_common_headers
#define send_asset web_send_asset
#define send_json web_send_json
#define send_json_error web_send_json_error
#define send_ok web_send_ok
static const char *TAG = "monitor_web";
static httpd_handle_t s_stream_server;
#if CONFIG_MCU_ENABLE_STORAGE
static httpd_handle_t s_file_server;
#endif
static SemaphoreHandle_t s_action_lock;
static atomic_bool s_change_pending, s_restarting;
static atomic_int s_change_error;
static bool s_started;
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[] asm("_binary_styles_css_end");
extern const uint8_t api_js_start[] asm("_binary_api_js_start");
extern const uint8_t api_js_end[] asm("_binary_api_js_end");
extern const uint8_t monitor_js_start[] asm("_binary_monitor_js_start");
extern const uint8_t monitor_js_end[] asm("_binary_monitor_js_end");

static const char *http_method_name(httpd_method_t method)
{
    switch (method) {
    case HTTP_GET:
        return "GET";
    case HTTP_POST:
        return "POST";
    default:
        return "OTHER";
    }
}

static void log_http_request(const httpd_req_t *req, const char *route)
{
    ESP_LOGI(TAG, "HTTP %s %s -> %s", http_method_name(req->method), req->uri, route);
}

static esp_err_t index_handler(httpd_req_t *req)
{
    log_http_request(req, "monitor page");
    return send_asset(req, index_html_start, index_html_end, "text/html; charset=utf-8");
}

static esp_err_t styles_handler(httpd_req_t *req)
{
    log_http_request(req, "stylesheet");
    return send_asset(req, styles_css_start, styles_css_end, "text/css; charset=utf-8");
}

static esp_err_t api_js_handler(httpd_req_t *req)
{
    log_http_request(req, "API script");
    return send_asset(req, api_js_start, api_js_end, "application/javascript; charset=utf-8");
}

static esp_err_t monitor_js_handler(httpd_req_t *req)
{
    log_http_request(req, "monitor script");
    return send_asset(req, monitor_js_start, monitor_js_end, "application/javascript; charset=utf-8");
}

static esp_err_t stream_redirect_handler(httpd_req_t *req)
{
    char host[64] = "192.168.4.1";
    size_t host_length = httpd_req_get_hdr_value_len(req, "Host");
    if (host_length > 0 && host_length < sizeof(host)) {
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
        char *port = strrchr(host, ':');
        if (port) {
            *port = '\0';
        }
    }

    char location[96];
    snprintf(location, sizeof(location), "http://%s:%d/stream", host, APP_STREAM_PORT);
    log_http_request(req, "MJPEG stream redirect");
    set_common_headers(req);
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t capture_handler(httpd_req_t *req)
{
    if (!camera_service_is_ready()) {
        return send_json_error(req, "409 Conflict", "camera_disabled",
                               "Enable the camera before capturing a photo");
    }
    camera_fb_t *frame = camera_service_get_frame();
    if (!frame) {
        return send_json_error(req, "503 Service Unavailable", "camera_unavailable",
                               "Camera frame is unavailable");
    }

    camera_fb_t photo = *frame;
    photo.buf = heap_caps_malloc(frame->len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!photo.buf) {
        camera_service_return_frame(frame);
        return send_json_error(req, "503 Service Unavailable", "capture_memory_unavailable",
                               "Not enough PSRAM to process the captured image");
    }
    memcpy(photo.buf, frame->buf, frame->len);
    camera_service_return_frame(frame);

#if CONFIG_MCU_ENABLE_STORAGE
    char saved_path[96] = {0};
    esp_err_t save_result = storage_service_save_photo(photo.buf, photo.len, saved_path, sizeof(saved_path));
    if (save_result == ESP_OK) {
        httpd_resp_set_hdr(req, "X-Saved-Path", saved_path);
        ESP_LOGI(TAG, "Photo saved: %s", saved_path);
    } else {
        httpd_resp_set_hdr(req, "X-Save-Error", esp_err_to_name(save_result));
        ESP_LOGW(TAG, "Photo was not saved to SD card: %s", esp_err_to_name(save_result));
    }

#endif
    set_common_headers(req);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%lld.%06ld", (long long)photo.timestamp.tv_sec,
             (long)photo.timestamp.tv_usec);
    httpd_resp_set_hdr(req, "X-Timestamp", timestamp);
    esp_err_t result = httpd_resp_send(req, (const char *)photo.buf, photo.len);
    heap_caps_free(photo.buf);
    return result;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    log_http_request(req, "MJPEG stream");
    if (!camera_service_is_ready()) {
        return send_json_error(req, "409 Conflict", "camera_disabled",
                               "Enable the camera before opening the stream");
    }
    if (!camera_service_claim_stream()) {
        return send_json_error(req, "503 Service Unavailable", "stream_busy",
                               "Only one live stream is supported");
    }

    static const char *content_type = "multipart/x-mixed-replace;boundary=frame";
    static const char *boundary = "\r\n--frame\r\n";
    char header[96];
    set_common_headers(req);
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t result = ESP_OK;
    bool camera_disabled = false;
    while (result == ESP_OK) {
        if (camera_service_stream_stop_requested()) {
            break;
        }
        camera_fb_t *frame = camera_service_get_frame();
        if (!frame) {
            if (!camera_service_is_ready()) {
                camera_disabled = true;
                break;
            }
            ESP_LOGW(TAG, "Camera frame capture failed");
            result = ESP_FAIL;
            break;
        }
        int header_length = snprintf(header, sizeof(header),
                                     "Content-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", frame->len);
        if (header_length < 0 || (size_t)header_length >= sizeof(header)) {
            camera_service_return_frame(frame);
            result = ESP_ERR_INVALID_SIZE;
            break;
        }
        result = httpd_resp_send_chunk(req, boundary, strlen(boundary));
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(req, header, header_length);
        }
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(req, (const char *)frame->buf, frame->len);
        }
        camera_service_return_frame(frame);
        if (result == ESP_OK && camera_service_stream_stop_requested()) {
            break;
        }
    }
    camera_service_release_stream();
    if (camera_disabled) {
        // Complete the chunked response cleanly when the web switch turns the
        // camera off. The client can then close without a URI-handler warning.
        httpd_resp_send_chunk(req, NULL, 0);
        return ESP_OK;
    }
    return result;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    wifi_manager_status_t wifi;
    wifi_manager_get_status(&wifi);
    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *document = cJSON_CreateObject();
    if (!document) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddStringToObject(document, "device_name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(document, "ip", wifi.ip);
    cJSON_AddStringToObject(document, "hostname", APP_DEVICE_HOSTNAME ".local");
    cJSON_AddStringToObject(document, "sensor", camera_service_sensor_name());
    cJSON_AddBoolToObject(document, "camera_ready", camera_service_is_ready());
    cJSON_AddBoolToObject(document, "camera_enabled", camera_service_is_enabled());
    cJSON_AddNumberToObject(document, "framesize", camera_service_get_framesize());
    cJSON_AddBoolToObject(document, "ap_active", wifi.ap_active);
    cJSON_AddStringToObject(document, "ssid", wifi.ssid);
    cJSON_AddNumberToObject(document, "stream_port", APP_STREAM_PORT);
    cJSON_AddStringToObject(document, "firmware_version", app ? app->version : "unknown");
    cJSON_AddNumberToObject(document, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(document, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(document, "min_free_heap_bytes", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(document, "reset_reason", esp_reset_reason());
    cJSON_AddBoolToObject(document, "camera_change_pending", atomic_load(&s_change_pending));
    cJSON_AddNumberToObject(document, "camera_change_error", atomic_load(&s_change_error));
#ifdef CONFIG_MCU_ENABLE_STORAGE
    cJSON_AddBoolToObject(document, "storage_available", true);
#else
    cJSON_AddBoolToObject(document, "storage_available", false);
#endif
#ifdef CONFIG_MCU_ENABLE_RECORDING
    cJSON_AddBoolToObject(document, "recording_available", true);
#else
    cJSON_AddBoolToObject(document, "recording_available", false);
#endif
    return send_json(req, document);
}

static esp_err_t control_handler(httpd_req_t *req)
{
    char query[64];
    char value[12];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "framesize", value, sizeof(value)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "missing_framesize", "framesize is required");
    }
    char *end = NULL;
    long frame_size = strtol(value, &end, 10);
    if (!end || *end != '\0' || frame_size < FRAMESIZE_QQVGA || frame_size > FRAMESIZE_UXGA) {
        return send_json_error(req, "400 Bad Request", "unsupported_framesize",
                               "Unsupported frame size");
    }

    if (atomic_load(&s_change_pending) || atomic_load(&s_restarting))
        return send_json_error(req, "409 Conflict", "camera_busy", "Camera state is changing");
    esp_err_t result = camera_service_set_framesize((framesize_t)frame_size);
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "recording_active",
                               "Stop recording before changing resolution");
    }
    if (result == ESP_ERR_TIMEOUT) {
        return send_json_error(req, "409 Conflict", "stream_busy",
                               "Video stream is still stopping; try again shortly");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "camera_unavailable",
                               "Unable to change camera resolution");
    }
    return send_ok(req, "Camera resolution updated");
}

static esp_err_t stop_monitor(void)
{
#if CONFIG_MCU_ENABLE_RECORDING
    esp_err_t result = recording_service_stop_and_wait(15000);
    if (result != ESP_OK) return result;
#endif
    camera_service_request_stream_stop();
    return camera_service_set_enabled(false);
}

esp_err_t monitor_web_prepare_restart(void)
{
    atomic_store(&s_restarting, true);
    if (!s_action_lock || xSemaphoreTake(s_action_lock, pdMS_TO_TICKS(30000)) != pdTRUE) {
        atomic_store(&s_restarting, false);
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t result = stop_monitor();
    xSemaphoreGive(s_action_lock);
    if (result != ESP_OK) atomic_store(&s_restarting, false);
    return result;
}

static void camera_change_task(void *arg)
{
    bool enabled = (bool)(uintptr_t)arg;
    xSemaphoreTake(s_action_lock, portMAX_DELAY);
    esp_err_t result = enabled && !atomic_load(&s_restarting)
                           ? camera_service_set_enabled(true) : stop_monitor();
    atomic_store(&s_change_error, result);
    xSemaphoreGive(s_action_lock);
    atomic_store(&s_change_pending, false);
    vTaskDelete(NULL);
}

static esp_err_t camera_toggle_handler(httpd_req_t *req)
{
    char query[32], value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "enabled", value, sizeof(value)) != ESP_OK ||
        (strcmp(value, "0") != 0 && strcmp(value, "1") != 0))
        return send_json_error(req, "400 Bad Request", "invalid_camera_state", "enabled must be 0 or 1");
    bool expected = false;
    if (atomic_load(&s_restarting) ||
        !atomic_compare_exchange_strong(&s_change_pending, &expected, true))
        return send_json_error(req, "409 Conflict", "camera_busy", "Camera state is changing");
    bool enabled = strcmp(value, "1") == 0;
    atomic_store(&s_change_error, ESP_OK);
    if (xTaskCreate(camera_change_task, "camera_change", 4096, (void *)(uintptr_t)enabled, 5, NULL) != pdPASS) {
        atomic_store(&s_change_pending, false);
        return send_json_error(req, "503 Service Unavailable", "no_memory", "Unable to schedule camera change");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return send_ok(req, "Camera state change requested");
}

#if CONFIG_MCU_ENABLE_RECORDING
static esp_err_t record_start_handler(httpd_req_t *req)
{
    if (atomic_load(&s_restarting) || atomic_load(&s_change_pending) ||
        xSemaphoreTake(s_action_lock, 0) != pdTRUE)
        return send_json_error(req, "409 Conflict", "camera_busy", "Camera state is changing");
    esp_err_t result = recording_service_start();
    xSemaphoreGive(s_action_lock);
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "recording_busy",
                               "Recording or SD file transfer is already active");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "recording_unavailable",
                               "Unable to start recording");
    }
    return send_ok(req, "Recording started");
}

static esp_err_t record_stop_handler(httpd_req_t *req)
{
    esp_err_t result = recording_service_stop();
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "recording_inactive", "Recording is not active");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "stop_failed",
                               "Unable to stop recording");
    }
    return send_ok(req, "Recording is stopping");
}

static esp_err_t record_status_handler(httpd_req_t *req)
{
    storage_status_t storage_status;
    storage_service_get_status(&storage_status);
    recording_status_t status;
    recording_service_get_status(&status);
    bool storage_busy = storage_service_is_busy();
    cJSON *document = cJSON_CreateObject();
    if (!document) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddStringToObject(document, "state", recording_service_state_name(status.state));
    bool active = status.state == RECORDING_STATE_STARTING ||
                  status.state == RECORDING_STATE_ACTIVE ||
                  status.state == RECORDING_STATE_STOPPING;
    cJSON_AddBoolToObject(document, "active", active);
    cJSON_AddBoolToObject(document, "storage_busy", storage_busy);
    cJSON_AddBoolToObject(document, "camera_enabled", camera_service_is_enabled());
    cJSON_AddBoolToObject(document, "sd_ready", storage_status.sd_ready);
    cJSON_AddNumberToObject(document, "sd_total_bytes", (double)storage_status.sd_total_bytes);
    cJSON_AddNumberToObject(document, "sd_free_bytes", (double)storage_status.sd_free_bytes);
    cJSON_AddStringToObject(document, "sd_error", storage_status.sd_error);
    cJSON_AddNumberToObject(document, "segment_minutes", status.segment_minutes);
    cJSON_AddNumberToObject(document, "frames", status.current_frames);
    cJSON_AddStringToObject(document, "path", status.path);
    cJSON_AddStringToObject(document, "last_error", status.last_error);
    return send_json(req, document);
}

static esp_err_t record_settings_handler(httpd_req_t *req)
{
    char query[32];
    char value[8];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "minutes", value, sizeof(value)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "missing_minutes", "minutes is required");
    }
    char *end = NULL;
    long minutes = strtol(value, &end, 10);
    if (!end || *end != '\0' || minutes < APP_RECORD_SEGMENT_MINUTES_MIN ||
        minutes > APP_RECORD_SEGMENT_MINUTES_MAX) {
        return send_json_error(req, "400 Bad Request", "invalid_minutes",
                               "minutes must be between 1 and 60");
    }

    esp_err_t result = recording_service_set_segment_minutes((uint16_t)minutes);
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "recording_active",
                               "Stop recording before changing segment duration");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "settings_failed",
                               "Unable to save recording settings");
    }
    return send_ok(req, "Recording segment duration updated");
}


#endif
#if CONFIG_MCU_ENABLE_STORAGE
static esp_err_t storage_status_handler(httpd_req_t *req)
{
    storage_status_t status;
    storage_service_get_status(&status);
    cJSON *document = cJSON_CreateObject();
    if (!document) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(document, "sd_ready", status.sd_ready);
    cJSON_AddBoolToObject(document, "busy", storage_service_is_busy());
    cJSON_AddNumberToObject(document, "total_bytes", (double)status.sd_total_bytes);
    cJSON_AddNumberToObject(document, "free_bytes", (double)status.sd_free_bytes);
    cJSON_AddStringToObject(document, "sd_error", status.sd_error);
    return send_json(req, document);
}

static esp_err_t sd_file_redirect_handler(httpd_req_t *req)
{
    char host[64] = "192.168.4.1";
    size_t host_length = httpd_req_get_hdr_value_len(req, "Host");
    if (host_length > 0 && host_length < sizeof(host) &&
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        char *port = strrchr(host, ':');
        if (port) {
            *port = '\0';
        }
    }

    char location[640];
    int length = snprintf(location, sizeof(location), "http://%s:%d%s", host, APP_FILE_PORT,
                          req->uri);
    if (length < 0 || (size_t)length >= sizeof(location)) {
        return send_json_error(req, "414 URI Too Long", "file_uri_too_long",
                               "The requested file URI is too long");
    }
    set_common_headers(req);
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t sd_check_handler(httpd_req_t *req)
{
    esp_err_t result = storage_service_check_sd();
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "recording_active",
                               "Stop recording before checking the SD card");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "sd_check_failed",
                               esp_err_to_name(result));
    }
    return send_ok(req, "SD card check passed");
}

static const char *sd_file_content_type(const char *name)
{
    const char *extension = strrchr(name, '.');
    if (!extension) {
        return "application/octet-stream";
    }
    if (strcasecmp(extension, ".jpg") == 0 || strcasecmp(extension, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(extension, ".avi") == 0 || strcasecmp(extension, ".part") == 0) {
        return "video/x-msvideo";
    }
    return "application/octet-stream";
}

static esp_err_t sd_files_handler(httpd_req_t *req)
{
    if (storage_service_is_busy()) {
        return send_json_error(req, "409 Conflict", "storage_busy",
                               "Stop recording or wait for the current download");
    }
    storage_file_info_t *files = calloc(APP_SD_FILE_LIST_LIMIT, sizeof(*files));
    if (!files) {
        return httpd_resp_send_500(req);
    }
    size_t count = 0;
    bool truncated = false;
    esp_err_t result = storage_service_list_files(files, APP_SD_FILE_LIST_LIMIT, &count, &truncated);
    if (result != ESP_OK) {
        free(files);
        if (result == ESP_ERR_INVALID_STATE) {
            return send_json_error(req, "409 Conflict", "storage_busy",
                                   "Stop recording or wait for the current download");
        }
        return send_json_error(req, "503 Service Unavailable", "sd_list_failed",
                               esp_err_to_name(result));
    }

    storage_status_t status;
    storage_service_get_status(&status);
    cJSON *document = cJSON_CreateObject();
    cJSON *items = document ? cJSON_AddArrayToObject(document, "files") : NULL;
    if (!document || !items) {
        free(files);
        cJSON_Delete(document);
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(document, "sd_ready", status.sd_ready);
    cJSON_AddNumberToObject(document, "total_bytes", (double)status.sd_total_bytes);
    cJSON_AddNumberToObject(document, "free_bytes", (double)status.sd_free_bytes);
    cJSON_AddBoolToObject(document, "recording_active",
                          storage_service_is_busy());
    cJSON_AddBoolToObject(document, "truncated", truncated);
    for (size_t index = 0; index < count; ++index) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            continue;
        }
        cJSON_AddStringToObject(item, "name", files[index].name);
        cJSON_AddNumberToObject(item, "size_bytes", (double)files[index].size_bytes);
        cJSON_AddItemToArray(items, item);
    }
    free(files);
    return send_json(req, document);
}

typedef struct {
    httpd_req_t *req;
    const char *name;
    bool response_started;
    char disposition[192];
} sd_file_response_t;

static esp_err_t prepare_sd_file_response(sd_file_response_t *response)
{
    set_common_headers(response->req);
    esp_err_t result = httpd_resp_set_type(response->req, sd_file_content_type(response->name));
    if (result != ESP_OK) {
        return result;
    }
    result = httpd_resp_set_hdr(response->req, "Cache-Control", "no-store");
    if (result != ESP_OK) {
        return result;
    }
    snprintf(response->disposition, sizeof(response->disposition), "attachment; filename=\"%s\"",
             response->name);
    result = httpd_resp_set_hdr(response->req, "Content-Disposition", response->disposition);
    if (result == ESP_OK) {
        response->response_started = true;
    }
    return result;
}

static esp_err_t send_file_chunk(const uint8_t *data, size_t length, void *context)
{
    sd_file_response_t *response = context;
    if (!response->response_started) {
        esp_err_t result = prepare_sd_file_response(response);
        if (result != ESP_OK) {
            return result;
        }
    }
    return httpd_resp_send_chunk(response->req, (const char *)data, length);
}

static esp_err_t sd_file_handler(httpd_req_t *req)
{
    if (storage_service_is_busy()) {
        return send_json_error(req, "409 Conflict", "recording_active",
                               "Stop recording before downloading SD files");
    }
    char query[512];
    char name[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "missing_file_name", "name is required");
    }
    sd_file_response_t response = {
        .req = req,
        .name = name,
    };
    esp_err_t result = storage_service_stream_file(name, send_file_chunk, &response);
    if (result != ESP_OK) {
        if (response.response_started) {
            // Headers/body were already committed. The socket error is
            // reported by the HTTP server; sending another response is not
            // possible and would produce a misleading URI-handler warning.
            ESP_LOGI(TAG, "SD file client disconnected while reading '%s': %s", name,
                     esp_err_to_name(result));
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Unable to stream SD file '%s': %s", name, esp_err_to_name(result));
        if (result == ESP_ERR_INVALID_ARG) {
            return send_json_error(req, "400 Bad Request", "invalid_file_name",
                                   "The requested file name is invalid");
        }
        if (result == ESP_ERR_NOT_FOUND) {
            return send_json_error(req, "404 Not Found", "file_not_found",
                                   "The requested SD file was not found");
        }
        if (result == ESP_ERR_INVALID_STATE) {
            return send_json_error(req, "409 Conflict", "storage_busy",
                                   "Stop recording before downloading SD files");
        }
        return send_json_error(req, "503 Service Unavailable", "sd_read_failed",
                               esp_err_to_name(result));
    }
    if (!response.response_started && prepare_sd_file_response(&response) != ESP_OK) {
        return ESP_FAIL;
    }
    result = httpd_resp_send_chunk(req, NULL, 0);
    if (result != ESP_OK) {
        ESP_LOGI(TAG, "SD file client disconnected after reading '%s': %s", name,
                 esp_err_to_name(result));
    }
    return ESP_OK;
}

static esp_err_t sd_file_delete_handler(httpd_req_t *req)
{
    char query[512];
    char name[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "name", name, sizeof(name)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "missing_file_name", "name is required");
    }
    esp_err_t result = storage_service_delete_file(name);
    if (result == ESP_ERR_INVALID_ARG) {
        return send_json_error(req, "400 Bad Request", "invalid_file_name",
                               "The requested file name is invalid");
    }
    if (result == ESP_ERR_NOT_FOUND) {
        return send_json_error(req, "404 Not Found", "file_not_found",
                               "The requested SD file was not found");
    }
    if (result == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "storage_busy",
                               "Stop recording or wait for the current download");
    }
    if (result != ESP_OK) {
        return send_json_error(req, "503 Service Unavailable", "delete_failed",
                               esp_err_to_name(result));
    }
    return send_ok(req, "SD file deleted");
}


#endif
static const httpd_uri_t s_routes[] = {
    {.uri="/camera/", .method=HTTP_GET, .handler=index_handler},
    {.uri="/camera/index.html", .method=HTTP_GET, .handler=index_handler},
    {.uri="/camera/styles.css", .method=HTTP_GET, .handler=styles_handler},
    {.uri="/camera/api.js", .method=HTTP_GET, .handler=api_js_handler},
    {.uri="/camera/monitor.js", .method=HTTP_GET, .handler=monitor_js_handler},
    {.uri="/api/camera/capture", .method=HTTP_GET, .handler=capture_handler},
    {.uri="/api/camera/stream", .method=HTTP_GET, .handler=stream_redirect_handler},
    {.uri="/api/camera/status", .method=HTTP_GET, .handler=status_handler},
    {.uri="/api/camera/framesize", .method=HTTP_GET, .handler=control_handler},
    {.uri="/api/camera/enabled", .method=HTTP_POST, .handler=camera_toggle_handler},
#if CONFIG_MCU_ENABLE_RECORDING
    {.uri="/api/recording/start", .method=HTTP_POST, .handler=record_start_handler},
    {.uri="/api/recording/stop", .method=HTTP_POST, .handler=record_stop_handler},
    {.uri="/api/recording/status", .method=HTTP_GET, .handler=record_status_handler},
    {.uri="/api/recording/settings", .method=HTTP_POST, .handler=record_settings_handler},
#endif
#if CONFIG_MCU_ENABLE_STORAGE
    {.uri="/api/storage/status", .method=HTTP_GET, .handler=storage_status_handler},
    {.uri="/api/storage/check", .method=HTTP_POST, .handler=sd_check_handler},
    {.uri="/api/storage/files", .method=HTTP_GET, .handler=sd_files_handler},
    {.uri="/api/storage/file", .method=HTTP_GET, .handler=sd_file_redirect_handler},
    {.uri="/api/storage/file", .method=HTTP_DELETE, .handler=sd_file_delete_handler},
#endif
};

esp_err_t monitor_web_start(void)
{
    if (s_started) return ESP_OK;
    httpd_handle_t control = web_server_handle();
    if (!control) return ESP_ERR_INVALID_STATE;
    s_action_lock = xSemaphoreCreateMutex();
    if (!s_action_lock) return ESP_ERR_NO_MEM;
    size_t registered = 0;
    esp_err_t result;
    for (; registered < sizeof(s_routes)/sizeof(s_routes[0]); ++registered) {
        result = httpd_register_uri_handler(control, &s_routes[registered]);
        if (result != ESP_OK) goto failed;
    }
    httpd_config_t stream = HTTPD_DEFAULT_CONFIG();
    stream.server_port = APP_STREAM_PORT;
    stream.ctrl_port += 1;
    stream.stack_size = 6144;
    stream.max_uri_handlers = 1;
    stream.max_open_sockets = 2;
    stream.send_wait_timeout = 10;
    stream.lru_purge_enable = true;
    result = httpd_start(&s_stream_server, &stream);
    if (result != ESP_OK) goto failed;
    const httpd_uri_t stream_route = {.uri="/stream", .method=HTTP_GET, .handler=stream_handler};
    result = httpd_register_uri_handler(s_stream_server, &stream_route);
    if (result != ESP_OK) goto failed;
#if CONFIG_MCU_ENABLE_STORAGE
    httpd_config_t files = HTTPD_DEFAULT_CONFIG();
    files.server_port = APP_FILE_PORT;
    files.ctrl_port += 2;
    files.stack_size = 6144;
    files.max_uri_handlers = 1;
    files.max_open_sockets = 2;
    files.send_wait_timeout = 60;
    result = httpd_start(&s_file_server, &files);
    if (result != ESP_OK) goto failed;
    const httpd_uri_t file_route = {.uri="/api/storage/file", .method=HTTP_GET, .handler=sd_file_handler};
    result = httpd_register_uri_handler(s_file_server, &file_route);
    if (result != ESP_OK) goto failed;
#endif
    result = web_server_register_page("摄像头监控", "/camera/");
    if (result != ESP_OK) goto failed;
    s_started = true;
    return ESP_OK;
failed:
#if CONFIG_MCU_ENABLE_STORAGE
    if (s_file_server) { httpd_stop(s_file_server); s_file_server = NULL; }
#endif
    if (s_stream_server) { httpd_stop(s_stream_server); s_stream_server = NULL; }
    while (registered) {
        --registered;
        httpd_unregister_uri_handler(control, s_routes[registered].uri, s_routes[registered].method);
    }
    vSemaphoreDelete(s_action_lock);
    s_action_lock = NULL;
    return result;
}
