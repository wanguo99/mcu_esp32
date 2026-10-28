#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define UART_NUM_MAX 3
#define UART_DATA_8_BITS 3
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define UART_MODE_RS485_HALF_DUPLEX 1
typedef enum { UART_PARITY_DISABLE, UART_PARITY_EVEN=2, UART_PARITY_ODD=3 } uart_parity_t;
typedef enum { UART_STOP_BITS_1=1, UART_STOP_BITS_2=3 } uart_stop_bits_t;
typedef struct { int baud_rate, data_bits; uart_parity_t parity; uart_stop_bits_t stop_bits;
                 int flow_ctrl, source_clk; } uart_config_t;
bool uart_is_driver_installed(int);
esp_err_t uart_driver_install(int, int, int, int, void *, int);
esp_err_t uart_driver_delete(int);
esp_err_t uart_param_config(int, const uart_config_t *);
esp_err_t uart_set_pin(int, int, int, int, int);
esp_err_t uart_set_mode(int, int);
esp_err_t uart_wait_tx_done(int, uint32_t);
esp_err_t uart_flush_input(int);
int uart_write_bytes(int, const void *, size_t);
int uart_read_bytes(int, void *, uint32_t, uint32_t);
