#include "relay_uart.h"
#include "relay_uart_codec.h"
#include "peripheral_resources.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

struct relay_uart_instance {
    relay_uart_config_t config;
    peripheral_claim_t claim;
    SemaphoreHandle_t lock;
    relay_uart_channel_frames_t *frames;
    relay_uart_command_status_t *commands;
    int64_t ready_at_us;
};
static portMUX_TYPE s_port_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ports;

static TickType_t timeout_ticks(uint32_t ms)
{ return (TickType_t)((ms + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS); }

static void release_port(int port)
{
    portENTER_CRITICAL(&s_port_lock);
    s_ports &= ~(1U << port);
    portEXIT_CRITICAL(&s_port_lock);
}

static esp_err_t exchange(relay_uart_handle_t relay, const uint8_t *request, size_t size,
                           uint8_t *response, size_t expected, bool *sent)
{
    *sent = false;
    const relay_uart_config_t *config = &relay->config;
    int64_t wait_us = relay->ready_at_us - esp_timer_get_time();
    if (wait_us > 0) vTaskDelay(timeout_ticks((uint32_t)((wait_us + 999) / 1000)));
    TickType_t timeout = timeout_ticks(config->response_timeout_ms);
    esp_err_t result = uart_wait_tx_done(config->uart_port, timeout);
    if (result != ESP_OK) goto done;
    result = uart_flush_input(config->uart_port);
    if (result != ESP_OK) goto done;
    if (uart_write_bytes(config->uart_port, request, size) != (int)size) {
        result = ESP_FAIL;
        goto done;
    }
    result = uart_wait_tx_done(config->uart_port, timeout);
    if (result != ESP_OK) goto done;
    *sent = true;
    int64_t deadline = esp_timer_get_time() + config->response_timeout_ms * 1000LL;
    size_t received = 0;
    while (received < expected) {
        int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { result = ESP_ERR_TIMEOUT; break; }
        // Reading incrementally detects 5-byte Modbus exceptions without waiting
        // for the longer successful reply, and handles fragmented UART delivery.
        int count = uart_read_bytes(config->uart_port, response + received, 1,
                                     timeout_ticks((uint32_t)((remaining + 999) / 1000)));
        if (count < 0) { result = ESP_FAIL; break; }
        if (!count) { result = ESP_ERR_TIMEOUT; break; }
        received += (size_t)count;
        if (config->protocol == RELAY_UART_MODBUS_RTU && received == 2 && (response[1] & 0x80))
            expected = 5;
    }
    if (result == ESP_OK && config->protocol == RELAY_UART_MODBUS_RTU) {
        bool ignored;
        result = relay_modbus_response(request, response, received, &ignored);
    }
done:
    // At least 3.5 8E2 character times, and >=1.75 ms at higher baud rates.
    uint32_t gap_us = (42000000U + config->baud_rate - 1) / config->baud_rate;
    if (gap_us < 1750) gap_us = 1750;
    relay->ready_at_us = esp_timer_get_time() + gap_us;
    return result;
}

static bool valid_frame(const relay_uart_frame_t *frame, bool required)
{ return frame->length <= RELAY_UART_MAX_FRAME && (!required || frame->length); }

esp_err_t relay_uart_create(const relay_uart_config_t *config, relay_uart_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || config->uart_port < 0 || config->uart_port >= UART_NUM_MAX ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->tx_gpio) || config->rx_gpio < -1 ||
        (config->rx_gpio >= 0 && !GPIO_IS_VALID_GPIO(config->rx_gpio)) ||
        config->rts_gpio < -1 || (config->rts_gpio >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(config->rts_gpio)) ||
        config->tx_gpio == config->rx_gpio || config->tx_gpio == config->rts_gpio ||
        (config->rx_gpio >= 0 && config->rx_gpio == config->rts_gpio) ||
        config->baud_rate < 1200 || config->baud_rate > 1000000 ||
        config->response_timeout_ms > 10000 || !config->channel_count ||
        config->channel_count > RELAY_UART_MAX_CHANNELS ||
        (config->parity != UART_PARITY_DISABLE && config->parity != UART_PARITY_EVEN &&
         config->parity != UART_PARITY_ODD) ||
        (config->stop_bits != UART_STOP_BITS_1 && config->stop_bits != UART_STOP_BITS_2) ||
        (config->protocol != RELAY_UART_FIXED_FRAMES && config->protocol != RELAY_UART_MODBUS_RTU))
        return ESP_ERR_INVALID_ARG;
#if CONFIG_ESP_CONSOLE_UART
    if (config->uart_port == CONFIG_ESP_CONSOLE_UART_NUM) return ESP_ERR_INVALID_ARG;
#endif
    if (config->protocol == RELAY_UART_MODBUS_RTU) {
        if (config->rx_gpio < 0 || config->address < 1 || config->address > 247 ||
            (uint32_t)config->first_coil + config->channel_count > 65536) return ESP_ERR_INVALID_ARG;
    } else {
        if (!config->frames) return ESP_ERR_INVALID_ARG;
        for (unsigned i = 0; i < config->channel_count; ++i) {
            const relay_uart_channel_frames_t *f = &config->frames[i];
            if (!valid_frame(&f->on, true) || !valid_frame(&f->off, true) ||
                !valid_frame(&f->ack_on, false) || !valid_frame(&f->ack_off, false) ||
                (config->rx_gpio < 0 && (f->ack_on.length || f->ack_off.length)))
                return ESP_ERR_INVALID_ARG;
        }
    }
    relay_uart_handle_t relay = calloc(1, sizeof(*relay));
    if (!relay) return ESP_ERR_NO_MEM;
    relay->config = *config;
    if (!relay->config.response_timeout_ms) relay->config.response_timeout_ms = 500;
    relay->lock = xSemaphoreCreateMutex();
    relay->commands = calloc(config->channel_count, sizeof(*relay->commands));
    if (config->protocol == RELAY_UART_FIXED_FRAMES) {
        relay->frames = malloc(config->channel_count * sizeof(*relay->frames));
        if (relay->frames) memcpy(relay->frames, config->frames, config->channel_count * sizeof(*relay->frames));
    }
    esp_err_t result = ESP_ERR_NO_MEM;
    if (!relay->lock || !relay->commands || (config->protocol == RELAY_UART_FIXED_FRAMES && !relay->frames)) goto fail;
    relay->config.frames = relay->frames;
    relay->claim = (peripheral_claim_t){.gpios=UINT64_C(1) << config->tx_gpio, .ledc_timer=-1, .ledc_channel=-1};
    if (config->rx_gpio >= 0) relay->claim.gpios |= UINT64_C(1) << config->rx_gpio;
    if (config->rts_gpio >= 0) relay->claim.gpios |= UINT64_C(1) << config->rts_gpio;
    result = peripheral_resources_acquire(&relay->claim);
    if (result != ESP_OK) goto fail;
    portENTER_CRITICAL(&s_port_lock);
    bool busy = (s_ports & (1U << config->uart_port)) != 0;
    if (!busy) s_ports |= 1U << config->uart_port;
    portEXIT_CRITICAL(&s_port_lock);
    if (busy) { result = ESP_ERR_INVALID_STATE; goto release_claim; }
    if (uart_is_driver_installed(config->uart_port)) { result = ESP_ERR_INVALID_STATE; goto release_uart; }
    result = uart_driver_install(config->uart_port, 256, 0, 0, NULL, 0);
    if (result != ESP_OK) goto release_uart;
    uart_config_t uart = {.baud_rate=(int)config->baud_rate, .data_bits=UART_DATA_8_BITS,
        .parity=config->parity, .stop_bits=config->stop_bits, .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,
        .source_clk=UART_SCLK_DEFAULT};
    result = uart_param_config(config->uart_port, &uart);
    if (result == ESP_OK) result = uart_set_pin(config->uart_port, config->tx_gpio, config->rx_gpio,
                                                config->rts_gpio, UART_PIN_NO_CHANGE);
    if (result == ESP_OK && config->rts_gpio >= 0)
        result = uart_set_mode(config->uart_port, UART_MODE_RS485_HALF_DUPLEX);
    if (result != ESP_OK) {
        uart_driver_delete(config->uart_port);
        gpio_reset_pin(config->tx_gpio);
        if (config->rx_gpio >= 0) gpio_reset_pin(config->rx_gpio);
        if (config->rts_gpio >= 0) gpio_reset_pin(config->rts_gpio);
        goto release_uart;
    }
    *out = relay;
    return ESP_OK;
release_uart:
    release_port(config->uart_port);
release_claim:
    peripheral_resources_release(&relay->claim);
fail:
    if (relay->lock) vSemaphoreDelete(relay->lock);
    free(relay->frames);
    free(relay->commands);
    free(relay);
    return result;
}

esp_err_t relay_uart_set(relay_uart_handle_t relay, unsigned channel, bool on)
{
    if (!relay || channel >= relay->config.channel_count) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(relay->lock, portMAX_DELAY);
    relay_uart_command_status_t *status = &relay->commands[channel];
    *status = (relay_uart_command_status_t){.requested_on=on};
    uint8_t request[8], response[RELAY_UART_MAX_FRAME];
    const uint8_t *data;
    const relay_uart_frame_t *ack = NULL;
    size_t length, expected;
    if (relay->config.protocol == RELAY_UART_MODBUS_RTU) {
        relay_modbus_request(relay->config.address, relay->config.first_coil + channel, false, on, request);
        data = request; length = expected = 8;
    } else {
        const relay_uart_channel_frames_t *frames = &relay->frames[channel];
        const relay_uart_frame_t *command = on ? &frames->on : &frames->off;
        ack = on ? &frames->ack_on : &frames->ack_off;
        data = command->data; length = command->length; expected = ack->length;
    }
    esp_err_t result = exchange(relay, data, length, response, expected, &status->command_sent);
    if (result == ESP_OK && ack && expected && memcmp(ack->data, response, expected))
        result = ESP_ERR_INVALID_RESPONSE;
    status->acknowledged = result == ESP_OK && expected > 0;
    xSemaphoreGive(relay->lock);
    return result;
}

esp_err_t relay_uart_read(relay_uart_handle_t relay, unsigned channel, bool *on)
{
    if (!relay || !on || channel >= relay->config.channel_count) return ESP_ERR_INVALID_ARG;
    if (relay->config.protocol != RELAY_UART_MODBUS_RTU) return ESP_ERR_NOT_SUPPORTED;
    xSemaphoreTake(relay->lock, portMAX_DELAY);
    uint8_t request[8], response[8];
    relay_modbus_request(relay->config.address, relay->config.first_coil + channel, true, false, request);
    bool sent;
    esp_err_t result = exchange(relay, request, 8, response, 6, &sent);
    if (result == ESP_OK) *on = (response[3] & 1) != 0;
    xSemaphoreGive(relay->lock);
    return result;
}

esp_err_t relay_uart_get_command_status(relay_uart_handle_t relay, unsigned channel,
                                        relay_uart_command_status_t *out)
{
    if (!relay || !out || channel >= relay->config.channel_count) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(relay->lock, portMAX_DELAY);
    *out = relay->commands[channel];
    xSemaphoreGive(relay->lock);
    return ESP_OK;
}

esp_err_t relay_uart_destroy(relay_uart_handle_t relay)
{
    if (!relay) return ESP_ERR_INVALID_ARG;
    esp_err_t result = uart_driver_delete(relay->config.uart_port);
    if (result != ESP_OK) return result;
    gpio_reset_pin(relay->config.tx_gpio);
    if (relay->config.rx_gpio >= 0) gpio_reset_pin(relay->config.rx_gpio);
    if (relay->config.rts_gpio >= 0) gpio_reset_pin(relay->config.rts_gpio);
    peripheral_resources_release(&relay->claim);
    release_port(relay->config.uart_port);
    vSemaphoreDelete(relay->lock);
    free(relay->commands);
    free(relay->frames);
    free(relay);
    return ESP_OK;
}
