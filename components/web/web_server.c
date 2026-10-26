#include "web_server.h"
#include "app_config.h"
#include "board.h"
#include "wifi_manager.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>

static httpd_handle_t s_server;
static web_shutdown_callback_t s_shutdown;
static atomic_bool s_restarting;
static atomic_int s_restart_result;
typedef struct { const char *name, *href; } web_page_t;
static web_page_t s_pages[8];
static size_t s_page_count;
static portMUX_TYPE s_pages_lock = portMUX_INITIALIZER_UNLOCKED;
extern const uint8_t home_start[] asm("_binary_web_home_html_start");
extern const uint8_t home_end[] asm("_binary_web_home_html_end");

static esp_err_t home_handler(httpd_req_t *req)
{ return web_send_asset(req, home_start, home_end, "text/html; charset=utf-8"); }

static esp_err_t modules_handler(httpd_req_t *req)
{
    cJSON *list = cJSON_CreateArray();
    if (!list) return httpd_resp_send_500(req);
    web_page_t pages[8];
    portENTER_CRITICAL(&s_pages_lock);
    size_t count = s_page_count;
    memcpy(pages, s_pages, count * sizeof(pages[0]));
    portEXIT_CRITICAL(&s_pages_lock);
    for (size_t i = 0; i < count; ++i) {
        cJSON *page = cJSON_CreateObject();
        if (!page) { cJSON_Delete(list); return httpd_resp_send_500(req); }
        cJSON_AddStringToObject(page, "name", pages[i].name);
        cJSON_AddStringToObject(page, "href", pages[i].href);
        cJSON_AddItemToArray(list, page);
    }
    return web_send_json(req, list);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    wifi_manager_status_t wifi;
    wifi_manager_get_status(&wifi);
    cJSON *status = cJSON_CreateObject();
    cJSON *modules = status ? cJSON_AddObjectToObject(status, "modules") : NULL;
    if (!status || !modules) { cJSON_Delete(status); return httpd_resp_send_500(req); }
    cJSON_AddStringToObject(status, "name", APP_DEVICE_NAME);
    cJSON_AddStringToObject(status, "board", board_get_config()->name);
    cJSON_AddStringToObject(status, "hostname", APP_DEVICE_HOSTNAME ".local");
    cJSON_AddStringToObject(status, "ip", wifi.ip);
    cJSON_AddStringToObject(status, "firmware_version", esp_app_get_description()->version);
    cJSON_AddNumberToObject(status, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(status, "free_heap_bytes", esp_get_free_heap_size());
    cJSON_AddNumberToObject(status, "restart_error", atomic_load(&s_restart_result));
#ifdef CONFIG_MCU_ENABLE_NETWORK
    cJSON_AddBoolToObject(modules, "network", true);
#endif
#ifdef CONFIG_MCU_ENABLE_WEB
    cJSON_AddBoolToObject(modules, "web", true);
#endif
#ifdef CONFIG_MCU_ENABLE_CAMERA
    cJSON_AddBoolToObject(modules, "camera", true);
#endif
#ifdef CONFIG_MCU_ENABLE_STORAGE
    cJSON_AddBoolToObject(modules, "storage", true);
#endif
#ifdef CONFIG_MCU_ENABLE_RECORDING
    cJSON_AddBoolToObject(modules, "recording", true);
#endif
#ifdef CONFIG_MCU_ENABLE_SERVO
    cJSON_AddBoolToObject(modules, "servo", true);
#endif
    return web_send_json(req, status);
}

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_err_t result = s_shutdown ? s_shutdown() : ESP_OK;
    atomic_store(&s_restart_result, result);
    if (result == ESP_OK) esp_restart();
    ESP_LOGE("web", "Restart cancelled: shutdown failed: %s", esp_err_to_name(result));
    atomic_store(&s_restarting, false);
    vTaskDelete(NULL);
}

static esp_err_t restart_handler(httpd_req_t *req)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong(&s_restarting, &expected, true))
        return web_send_json_error(req, "409 Conflict", "restart_pending", "Restart already pending");
    if (xTaskCreate(restart_task, "system_restart", 4096, NULL, 5, NULL) != pdPASS) {
        atomic_store(&s_restarting, false);
        return web_send_json_error(req, "503 Service Unavailable", "restart_failed", "Unable to create task");
    }
    httpd_resp_set_status(req, "202 Accepted");
    return web_send_ok(req, "Restart requested; active writes will finish first");
}

esp_err_t web_server_start(web_shutdown_callback_t shutdown)
{
    if (s_server) return ESP_OK;
    s_shutdown = shutdown;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.max_uri_handlers = 64;
    config.max_open_sockets = CONFIG_LWIP_MAX_SOCKETS >= 11 ? 8 : CONFIG_LWIP_MAX_SOCKETS - 3;
    config.lru_purge_enable = true;
    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) return result;
    const httpd_uri_t routes[] = {
        {.uri="/", .method=HTTP_GET, .handler=home_handler},
        {.uri="/api/status", .method=HTTP_GET, .handler=status_handler},
        {.uri="/api/modules", .method=HTTP_GET, .handler=modules_handler},
        {.uri="/api/system/restart", .method=HTTP_POST, .handler=restart_handler},
    };
    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); ++i) {
        result = httpd_register_uri_handler(s_server, &routes[i]);
        if (result != ESP_OK) { web_server_stop(); return result; }
    }
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!s_server) return ESP_OK;
    if (atomic_load(&s_restarting)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = httpd_stop(s_server);
    if (result == ESP_OK) {
        s_server = NULL;
        portENTER_CRITICAL(&s_pages_lock);
        s_page_count = 0;
        portEXIT_CRITICAL(&s_pages_lock);
    }
    return result;
}

httpd_handle_t web_server_handle(void) { return s_server; }

esp_err_t web_server_register_page(const char *name, const char *href)
{
    if (!s_server || !name || !href) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_pages_lock);
    if (s_page_count == sizeof(s_pages)/sizeof(s_pages[0])) {
        portEXIT_CRITICAL(&s_pages_lock);
        return ESP_ERR_NO_MEM;
    }
    s_pages[s_page_count].name = name;
    s_pages[s_page_count++].href = href;
    portEXIT_CRITICAL(&s_pages_lock);
    return ESP_OK;
}
