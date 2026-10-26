#include "wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"

static const char *TAG = "wifi_manager";

static SemaphoreHandle_t s_lock;
static esp_netif_t *s_ap_netif;
static esp_event_handler_instance_t s_wifi_event_instance;
static wifi_manager_status_t s_status;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)event_data;
    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_AP_START) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ap_active = true;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "Access point started: ssid='%s', url=http://192.168.4.1",
                 CONFIG_CAM_WIFI_AP_SSID);
    } else if (event_id == WIFI_EVENT_AP_STOP) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.ap_active = false;
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "Access point stopped");
    } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Client connected to the access point");
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Client disconnected from the access point");
    }
}

static esp_err_t configure_access_point(void)
{
    size_t password_length = strlen(CONFIG_CAM_WIFI_AP_PASSWORD);
    ESP_RETURN_ON_FALSE(strlen(CONFIG_CAM_WIFI_AP_SSID) > 0 &&
                            strlen(CONFIG_CAM_WIFI_AP_SSID) <= 32,
                        ESP_ERR_INVALID_ARG, TAG, "Access point SSID must contain 1 to 32 characters");
    ESP_RETURN_ON_FALSE(password_length == 0 ||
                            (password_length >= 8 && password_length <= 63),
                        ESP_ERR_INVALID_ARG, TAG,
                        "Access point password must be empty or contain 8 to 63 characters");

    wifi_config_t config = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)config.ap.ssid, CONFIG_CAM_WIFI_AP_SSID, sizeof(config.ap.ssid));
    config.ap.ssid_len = strlen(CONFIG_CAM_WIFI_AP_SSID);
    if (password_length > 0) {
        strlcpy((char *)config.ap.password, CONFIG_CAM_WIFI_AP_PASSWORD,
                sizeof(config.ap.password));
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "Set AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &config), TAG,
                        "Set access point configuration failed");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_status.ssid, CONFIG_CAM_WIFI_AP_SSID, sizeof(s_status.ssid));
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t wifi_manager_init(void)
{
    if (s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Event loop initialization failed");
    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_config), TAG, "Wi-Fi initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL,
                            &s_wifi_event_instance),
                        TAG, "Wi-Fi event registration failed");

    s_ap_netif = esp_netif_create_default_wifi_ap();
    ESP_RETURN_ON_FALSE(s_ap_netif, ESP_ERR_NO_MEM, TAG,
                        "Create access point network interface failed");
    esp_err_t result = esp_netif_set_hostname(s_ap_netif, APP_DEVICE_HOSTNAME);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Set access point hostname failed: %s", esp_err_to_name(result));
    }

    result = mdns_init();
    if (result == ESP_OK || result == ESP_ERR_INVALID_STATE) {
        result = mdns_hostname_set(APP_DEVICE_HOSTNAME);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Set mDNS hostname failed: %s", esp_err_to_name(result));
        }
        result = mdns_instance_name_set(APP_DEVICE_NAME);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Set mDNS name failed: %s", esp_err_to_name(result));
        }
    } else {
        ESP_LOGW(TAG, "mDNS unavailable: %s; AP will remain available", esp_err_to_name(result));
    }

    ESP_RETURN_ON_ERROR(configure_access_point(), TAG, "Configure access point failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Start Wi-Fi failed");
    result = esp_wifi_set_ps(WIFI_PS_NONE);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Disable Wi-Fi power save failed: %s", esp_err_to_name(result));
    }
    return ESP_OK;
}

void wifi_manager_get_status(wifi_manager_status_t *status)
{
    if (!status) {
        return;
    }
    memset(status, 0, sizeof(*status));
    strlcpy(status->ip, "0.0.0.0", sizeof(status->ip));
    if (!s_lock) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_lock);

    esp_netif_ip_info_t ip_info = {0};
    if (s_ap_netif && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        esp_ip4addr_ntoa(&ip_info.ip, status->ip, sizeof(status->ip));
    }
}
