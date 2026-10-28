// Deterministic hardware substitutes; production drivers remain unmodified.
#include "fake_hardware.h"
#include "board.h"
#include "board_definition.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static _Atomic int64_t s_now = 1000000;
const board_config_t *board_get_config(void) { return &board_definition; }
void vTaskDelay(TickType_t ticks) { s_now += (int64_t)ticks * portTICK_PERIOD_MS * 1000; }
esp_err_t gpio_reset_pin(int gpio) { (void)gpio; return ESP_OK; }
esp_err_t gpio_set_level(int gpio,uint32_t level) { (void)gpio; (void)level; return ESP_OK; }
esp_err_t gpio_set_pull_mode(int gpio,gpio_pull_mode_t mode) { (void)gpio; (void)mode; return ESP_OK; }
esp_err_t gpio_input_enable(int gpio) { (void)gpio; return ESP_OK; }

unsigned test_duty;
static unsigned pending_duty;
bool test_inverted;
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) { (void)c; return ESP_OK; }
esp_err_t ledc_channel_config(const ledc_channel_config_t *c)
{ pending_duty=test_duty=c->duty; test_inverted=c->flags.output_invert; return ESP_OK; }
esp_err_t ledc_set_duty_and_update(int m, int c, uint32_t d, uint32_t h)
{ (void)m; (void)c; (void)d; (void)h; return ESP_FAIL; } // No global fade service installed.
esp_err_t ledc_set_duty(int m, int c, uint32_t d)
{ (void)m; (void)c; pending_duty=d; return ESP_OK; }
esp_err_t ledc_update_duty(int m, int c)
{ (void)m; (void)c; test_duty=pending_duty; return ESP_OK; }
esp_err_t ledc_stop(int m, int c, uint32_t idle)
{ (void)m; (void)c; test_duty=idle; return ESP_OK; }
uint32_t ledc_get_freq(int m,int t) { (void)m;(void)t; return 50; }
uint32_t ledc_get_duty(int m,int c) { (void)m;(void)c; return test_duty; }

struct test_counter { bool enabled, started, watch; };
struct test_channel { pcnt_unit_handle_t unit; };
int test_fail_counter_channel, test_counter_count;
esp_err_t pcnt_new_unit(const pcnt_unit_config_t *c, pcnt_unit_handle_t *out)
{ assert(c->flags.accum_count); *out=calloc(1,sizeof(**out)); return *out ? ESP_OK : ESP_ERR_NO_MEM; }
esp_err_t pcnt_new_channel(pcnt_unit_handle_t u, const pcnt_chan_config_t *c, pcnt_channel_handle_t *out)
{
    assert(c->level_gpio_num == -1);
    if (test_fail_counter_channel) return ESP_FAIL;
    *out=malloc(sizeof(**out)); (*out)->unit=u; return ESP_OK;
}
esp_err_t pcnt_channel_set_edge_action(pcnt_channel_handle_t c, int rise, int fall)
{ (void)c; assert(rise==0 && fall==1); return ESP_OK; }
esp_err_t pcnt_channel_set_level_action(pcnt_channel_handle_t c, int high, int low)
{ (void)c; assert(high==0 && low==0); return ESP_OK; }
esp_err_t pcnt_unit_set_glitch_filter(pcnt_unit_handle_t u, const pcnt_glitch_filter_config_t *f)
{ (void)u; assert(f->max_glitch_ns==1000); return ESP_OK; }
esp_err_t pcnt_unit_add_watch_point(pcnt_unit_handle_t u, int limit)
{ assert(limit==30000); u->watch=true; return ESP_OK; }
esp_err_t pcnt_unit_remove_watch_point(pcnt_unit_handle_t u, int limit)
{ (void)limit; assert(u->watch); u->watch=false; return ESP_OK; }
esp_err_t pcnt_unit_enable(pcnt_unit_handle_t u) { u->enabled=true; return ESP_OK; }
esp_err_t pcnt_unit_disable(pcnt_unit_handle_t u) { assert(!u->started); u->enabled=false; return ESP_OK; }
esp_err_t pcnt_unit_clear_count(pcnt_unit_handle_t u) { (void)u; test_counter_count=0; return ESP_OK; }
esp_err_t pcnt_unit_start(pcnt_unit_handle_t u) { assert(u->enabled); u->started=true; return ESP_OK; }
esp_err_t pcnt_unit_stop(pcnt_unit_handle_t u) { u->started=false; return ESP_OK; }
esp_err_t pcnt_unit_get_count(pcnt_unit_handle_t u, int *out)
{ assert(u->started); *out=test_counter_count; return ESP_OK; }
esp_err_t pcnt_del_channel(pcnt_channel_handle_t c) { free(c); return ESP_OK; }
esp_err_t pcnt_del_unit(pcnt_unit_handle_t u) { assert(!u->enabled && !u->watch); free(u); return ESP_OK; }

struct test_timer { esp_timer_create_args_t args; bool active; };
static esp_timer_handle_t s_timer;
int64_t esp_timer_get_time(void) { return s_now; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{ *out=calloc(1,sizeof(**out)); (*out)->args=*args; s_timer=*out; return ESP_OK; }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t interval)
{ assert(interval>=100000); t->active=true; return ESP_OK; }
esp_err_t esp_timer_stop_blocking(esp_timer_handle_t t, uint32_t timeout)
{ (void)timeout; t->active=false; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t t)
{ assert(!t->active); free(t); s_timer=NULL; return ESP_OK; }
void test_advance_time(uint32_t ms, bool sample)
{ s_now+=(int64_t)ms*1000; if (sample && s_timer && s_timer->active) s_timer->args.callback(s_timer->args.arg); }

unsigned test_uart_writes, test_uart_reads, test_uart_last_size;
uint8_t test_uart_last[64];
int test_uart_reply_mode;
static bool s_uart[UART_NUM_MAX], s_fixed;
static uint8_t s_reply[64], s_fixed_reply[64];
static unsigned s_reply_size, s_reply_pos, s_fixed_size;
static void append_crc(uint8_t *bytes, unsigned size)
{
    uint16_t crc=0xffff;
    for (unsigned i=0;i<size;++i) {
        crc^=bytes[i];
        for (unsigned j=0;j<8;++j) crc=(crc&1) ? (crc>>1)^0xa001 : crc>>1;
    }
    bytes[size]=crc&0xff; bytes[size+1]=crc>>8;
}
bool uart_is_driver_installed(int port) { return s_uart[port]; }
esp_err_t uart_driver_install(int p, int rx, int tx, int q, void *queue, int flags)
{ (void)rx; (void)tx; (void)q; (void)queue; (void)flags; assert(!s_uart[p]); s_uart[p]=true; return ESP_OK; }
esp_err_t uart_driver_delete(int p) { assert(s_uart[p]); s_uart[p]=false; return ESP_OK; }
esp_err_t uart_param_config(int p, const uart_config_t *c) { (void)p; (void)c; return ESP_OK; }
esp_err_t uart_set_pin(int p, int tx, int rx, int rts, int cts)
{ (void)p; (void)tx; (void)rx; (void)rts; (void)cts; return ESP_OK; }
esp_err_t uart_set_mode(int p, int mode) { (void)p; (void)mode; return ESP_OK; }
esp_err_t uart_wait_tx_done(int p, uint32_t ticks) { (void)p; (void)ticks; return ESP_OK; }
esp_err_t uart_flush_input(int p) { (void)p; s_reply_size=s_reply_pos=0; return ESP_OK; }
void test_fixed_reply(const uint8_t *bytes, unsigned size)
{ s_fixed=true; s_fixed_size=size; if(size) memcpy(s_fixed_reply,bytes,size); }
int uart_write_bytes(int p, const void *data, size_t size)
{
    (void)p; ++test_uart_writes; assert(size<=64); memcpy(test_uart_last,data,size); test_uart_last_size=size;
    if (s_fixed) { s_reply_size=s_fixed_size; memcpy(s_reply,s_fixed_reply,s_fixed_size); }
    else if (test_uart_reply_mode != 1) {
        const uint8_t *request=data;
        if (test_uart_reply_mode==4) {
            s_reply[0]=request[0]; s_reply[1]=request[1]|0x80; s_reply[2]=2;
            append_crc(s_reply,3); s_reply_size=5;
        } else if (request[1]==0x05) { memcpy(s_reply,data,8); s_reply_size=8; }
        else { s_reply[0]=request[0]; s_reply[1]=1; s_reply[2]=1; s_reply[3]=1; append_crc(s_reply,4); s_reply_size=6; }
        if (test_uart_reply_mode==2) s_reply[s_reply_size-1]^=1;
        if (test_uart_reply_mode==3) { ++s_reply[0]; append_crc(s_reply,s_reply_size-2); }
    }
    return (int)size;
}
int uart_read_bytes(int p, void *out, uint32_t size, uint32_t ticks)
{
    (void)p; assert(size==1 && ticks>0); ++test_uart_reads;
    if(s_reply_pos==s_reply_size) { vTaskDelay(ticks); return 0; }
    *(uint8_t *)out=s_reply[s_reply_pos++]; return 1;
}

struct test_i2c_bus { unsigned devices; };
struct test_i2c_device { i2c_master_bus_handle_t bus; };
uint8_t test_i2c_registers[128];
unsigned test_i2c_buses, test_i2c_devices, test_i2c_reads, test_i2c_writes;
uint16_t test_i2c_address;
int test_i2c_error, test_i2c_fail_step;
static unsigned s_i2c_operations;
void test_i2c_reset(void)
{
    assert(!test_i2c_buses && !test_i2c_devices);
    memset(test_i2c_registers,0,sizeof(test_i2c_registers)); test_i2c_registers[0x75]=0x68;
    s_i2c_operations=test_i2c_reads=test_i2c_writes=0;
    test_i2c_error=test_i2c_fail_step=0;
}
static esp_err_t i2c_operation(void)
{
    ++s_i2c_operations;
    return test_i2c_error ? test_i2c_error :
        (test_i2c_fail_step>0 && s_i2c_operations==(unsigned)test_i2c_fail_step ? ESP_FAIL : ESP_OK);
}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c, i2c_master_bus_handle_t *out)
{
    assert(c->i2c_port==0); // Port 1 is reserved by the enabled camera in these tests.
    esp_err_t result=i2c_operation(); if(result) return result;
    *out=calloc(1,sizeof(**out)); ++test_i2c_buses; return ESP_OK;
}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b, const i2c_device_config_t *c, i2c_master_dev_handle_t *out)
{
    esp_err_t result=i2c_operation(); if(result) return result;
    assert(c->scl_speed_hz>0 && c->scl_speed_hz<=400000); test_i2c_address=c->device_address;
    *out=malloc(sizeof(**out)); (*out)->bus=b; ++b->devices; ++test_i2c_devices; return ESP_OK;
}
esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t d)
{ --d->bus->devices; --test_i2c_devices; free(d); return ESP_OK; }
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t b)
{ assert(!b->devices); free(b); --test_i2c_buses; return ESP_OK; }
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d, const uint8_t *data, size_t size, int timeout)
{
    (void)d; assert(size==2 && timeout>0); esp_err_t result=i2c_operation(); if(result) return result;
    ++test_i2c_writes; test_i2c_registers[data[0]]=data[1]; return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t d, const uint8_t *tx, size_t tx_size,
                                     uint8_t *rx, size_t rx_size, int timeout)
{
    (void)d; assert(tx_size==1 && timeout>0); esp_err_t result=i2c_operation(); if(result) return result;
    assert((*tx==0x75 && rx_size==1) || (*tx==0x3b && rx_size==14));
    ++test_i2c_reads; memcpy(rx,test_i2c_registers+*tx,rx_size); return ESP_OK;
}
