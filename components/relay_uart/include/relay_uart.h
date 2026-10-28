#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"

#define RELAY_UART_MAX_FRAME 64
#define RELAY_UART_MAX_CHANNELS 32
typedef struct relay_uart_instance *relay_uart_handle_t;
typedef enum { RELAY_UART_FIXED_FRAMES, RELAY_UART_MODBUS_RTU } relay_uart_protocol_t;
typedef struct { uint8_t data[RELAY_UART_MAX_FRAME]; size_t length; } relay_uart_frame_t;
typedef struct {
    relay_uart_frame_t on, off;
    relay_uart_frame_t ack_on, ack_off; // length=0 means transmit-only, not confirmed.
} relay_uart_channel_frames_t;
typedef struct {
    int uart_port, tx_gpio, rx_gpio; // rx_gpio=-1 is allowed only for send-only fixed frames.
    int rts_gpio; // -1 for TTL/full duplex; otherwise RS485 transceiver DE/~RE control.
    uint32_t baud_rate, response_timeout_ms; // 0 timeout selects 500 ms.
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    relay_uart_protocol_t protocol;
    uint16_t channel_count;
    uint8_t address; // Modbus slave address 1..247; broadcast is not supported.
    uint16_t first_coil; // Zero-based coil address; API channels are zero-based too.
    const relay_uart_channel_frames_t *frames; // Fixed frames copied by create().
} relay_uart_config_t;
#define RELAY_UART_MODBUS_CONFIG_DEFAULT(port_id, tx_pin, rx_pin, channels) { \
    .uart_port=(port_id), .tx_gpio=(tx_pin), .rx_gpio=(rx_pin), .rts_gpio=-1, \
    .baud_rate=9600, .response_timeout_ms=500, .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1, \
    .protocol=RELAY_UART_MODBUS_RTU, .channel_count=(channels), .address=1, .first_coil=0, .frames=NULL \
}
#define RELAY_UART_FIXED_CONFIG_DEFAULT(port_id, tx_pin, rx_pin, channels, frame_table) { \
    .uart_port=(port_id), .tx_gpio=(tx_pin), .rx_gpio=(rx_pin), .rts_gpio=-1, \
    .baud_rate=9600, .response_timeout_ms=500, .parity=UART_PARITY_DISABLE, .stop_bits=UART_STOP_BITS_1, \
    .protocol=RELAY_UART_FIXED_FRAMES, .channel_count=(channels), .frames=(frame_table) \
}
typedef struct {
    bool command_sent, requested_on, acknowledged;
} relay_uart_command_status_t;

// Acquires a UART and pins, but sends no relay command at creation or destruction.
esp_err_t relay_uart_create(const relay_uart_config_t *config, relay_uart_handle_t *out);
esp_err_t relay_uart_set(relay_uart_handle_t relay, unsigned channel, bool on);
// Modbus FC01 readback of one coil. Fixed-frame modules return NOT_SUPPORTED.
esp_err_t relay_uart_read(relay_uart_handle_t relay, unsigned channel, bool *on);
// Cached command metadata is not contact feedback. A failed command clears acknowledgment.
esp_err_t relay_uart_get_command_status(relay_uart_handle_t relay, unsigned channel,
                                        relay_uart_command_status_t *out);
// Caller must exclude concurrent users before destruction. Does not switch relay outputs.
esp_err_t relay_uart_destroy(relay_uart_handle_t relay);
