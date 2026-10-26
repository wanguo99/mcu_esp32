#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
typedef esp_err_t (*web_shutdown_callback_t)(void);
esp_err_t web_server_start(web_shutdown_callback_t shutdown);
esp_err_t web_server_stop(void);
httpd_handle_t web_server_handle(void);
esp_err_t web_server_register_page(const char *name, const char *href);
void web_set_common_headers(httpd_req_t *req);
esp_err_t web_send_asset(httpd_req_t *req, const uint8_t *start, const uint8_t *end, const char *content_type);
esp_err_t web_send_json(httpd_req_t *req, cJSON *document);
esp_err_t web_send_json_error(httpd_req_t *req, const char *status, const char *code, const char *message);
esp_err_t web_send_ok(httpd_req_t *req, const char *message);
