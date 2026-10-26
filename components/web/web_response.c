#include "web_server.h"
#include <string.h>
void web_set_common_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

esp_err_t web_send_asset(httpd_req_t *req, const uint8_t *start, const uint8_t *end,
                            const char *content_type)
{
    size_t length = end - start;
    if (length > 0 && start[length - 1] == '\0') {
        --length;
    }
    web_set_common_headers(req);
    httpd_resp_set_type(req, content_type);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)start, length);
}

esp_err_t web_send_json(httpd_req_t *req, cJSON *document)
{
    char *payload = cJSON_PrintUnformatted(document);
    cJSON_Delete(document);
    if (!payload) {
        httpd_resp_send_500(req);
        return ESP_ERR_NO_MEM;
    }
    web_set_common_headers(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t result = httpd_resp_sendstr(req, payload);
    cJSON_free(payload);
    return result;
}

esp_err_t web_send_json_error(httpd_req_t *req, const char *status, const char *code,
                                 const char *message)
{
    httpd_resp_set_status(req, status);
    cJSON *document = cJSON_CreateObject();
    if (!document) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddStringToObject(document, "error", code);
    cJSON_AddStringToObject(document, "message", message);
    return web_send_json(req, document);
}

esp_err_t web_send_ok(httpd_req_t *req, const char *message)
{
    cJSON *document = cJSON_CreateObject();
    if (!document) {
        return httpd_resp_send_500(req);
    }
    cJSON_AddBoolToObject(document, "ok", true);
    cJSON_AddStringToObject(document, "message", message);
    return web_send_json(req, document);
}
