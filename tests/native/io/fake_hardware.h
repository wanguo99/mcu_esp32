#pragma once
#include <stdbool.h>
#include <stdint.h>
extern unsigned test_duty;
extern bool test_inverted;
extern int test_fail_counter_channel, test_counter_count;
extern unsigned test_uart_writes, test_uart_reads;
extern uint8_t test_uart_last[64];
extern unsigned test_uart_last_size;
// 0=normal Modbus response, 1=timeout, 2=bad CRC, 3=wrong slave, 4=exception.
extern int test_uart_reply_mode;
void test_fixed_reply(const uint8_t *data, unsigned length);
void test_advance_time(uint32_t milliseconds, bool sample);
extern uint8_t test_i2c_registers[128];
extern unsigned test_i2c_buses, test_i2c_devices, test_i2c_reads, test_i2c_writes;
extern uint16_t test_i2c_address;
extern int test_i2c_error, test_i2c_fail_step;
void test_i2c_reset(void);
