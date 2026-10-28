// Execute the real worker on a host thread; use actual blocking notifications.
#include "fake_hardware.h"
#include "led_strip.h"
#include "board.h"
#include "board_definition.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

atomic_int test_active_strips;
atomic_bool test_fail_refresh, test_fail_device;
const board_config_t *board_get_config(void) { return &board_definition; }
esp_err_t gpio_reset_pin(int gpio) { (void)gpio; return ESP_OK; }
int64_t esp_timer_get_time(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000000+now.tv_nsec/1000;
}

struct test_led_strip { uint32_t count; ws2812_color_t pixels[]; };
static led_strip_handle_t s_strip;
ws2812_color_t test_led_pixel(unsigned index) { assert(s_strip && index<s_strip->count); return s_strip->pixels[index]; }
esp_err_t led_strip_new_rmt_device(const led_strip_config_t *c, const led_strip_rmt_config_t *rmt, led_strip_handle_t *out)
{
    assert(rmt->resolution_hz==10000000 && c->led_model==LED_MODEL_WS2812);
    if(atomic_load(&test_fail_device)) return ESP_FAIL;
    *out=calloc(1,sizeof(**out)+c->max_leds*sizeof(ws2812_color_t)); (*out)->count=c->max_leds;
    s_strip=*out; atomic_fetch_add(&test_active_strips,1); return ESP_OK;
}
esp_err_t led_strip_set_pixel(led_strip_handle_t strip, uint32_t i, uint32_t r, uint32_t g, uint32_t b)
{ assert(i<strip->count && r<=255 && g<=255 && b<=255); strip->pixels[i]=(ws2812_color_t){r,g,b}; return ESP_OK; }
esp_err_t led_strip_refresh(led_strip_handle_t strip)
{ (void)strip; return atomic_load(&test_fail_refresh) ? ESP_FAIL : ESP_OK; }
esp_err_t led_strip_clear(led_strip_handle_t strip)
{
    for(unsigned i=0;i<strip->count;++i) strip->pixels[i]=(ws2812_color_t){0};
    return led_strip_refresh(strip);
}
esp_err_t led_strip_del(led_strip_handle_t strip)
{ free(strip); s_strip=NULL; atomic_fetch_sub(&test_active_strips,1); return ESP_OK; }
