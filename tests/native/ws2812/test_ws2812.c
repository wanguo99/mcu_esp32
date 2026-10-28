#include "ws2812.h"
#include "ws2812_effects.h"
#include "fake_hardware.h"
#include "esp_timer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static void pause_ms(unsigned ms)
{ struct timespec wait={.tv_nsec=(long)ms*1000000}; nanosleep(&wait,NULL); }
static void wait_for_tasks(void)
{
    int64_t end=esp_timer_get_time()+1000000;
    while(atomic_load(&test_active_tasks)) { assert(esp_timer_get_time()<end); pause_ms(1); }
}
static void effects(void)
{
    ws2812_effect_config_t cfg={.effect=WS2812_EFFECT_SOLID,.color={255,128,64},.brightness=128};
    ws2812_color_t color=ws2812_effect_pixel(&cfg,4,0,0);
    assert(color.red==128 && color.green==64 && color.blue==32);
    cfg.brightness=255; cfg.effect=WS2812_EFFECT_BREATH; cfg.period_ms=1000;
    assert(ws2812_effect_pixel(&cfg,4,0,0).red==0);
    assert(ws2812_effect_pixel(&cfg,4,0,250000).red==63);
    assert(ws2812_effect_pixel(&cfg,4,0,500000).red==255);
    assert(ws2812_effect_pixel(&cfg,4,0,1000000).red==0);
    cfg.effect=WS2812_EFFECT_BLINK;
    assert(ws2812_effect_pixel(&cfg,4,0,0).red==255);
    assert(ws2812_effect_pixel(&cfg,4,0,500000).red==0);
    cfg.effect=WS2812_EFFECT_CHASE; cfg.tail_length=2;
    assert(ws2812_effect_pixel(&cfg,4,0,0).red==255);
    assert(ws2812_effect_pixel(&cfg,4,3,0).red==127);
    assert(ws2812_effect_pixel(&cfg,4,2,0).red==0);
    assert(ws2812_effect_pixel(&cfg,4,1,250000).red==255);
    cfg.reverse=true; assert(ws2812_effect_pixel(&cfg,4,3,0).red==255);
    assert(ws2812_effect_pixel(&cfg,4,0,0).red==127);
    cfg.effect=WS2812_EFFECT_OFF; assert(ws2812_effect_pixel(&cfg,4,0,0).red==0);
}
static void worker(void)
{
    ws2812_config_t cfg=WS2812_CONFIG_DEFAULT(1,8); ws2812_handle_t strip;
    assert(ws2812_create(&cfg,&strip)==ESP_OK);
    for(unsigned i=0;i<8;++i) assert(test_led_pixel(i).red==0);
    assert(ws2812_set_color(strip,(ws2812_color_t){255,128,64},128)==ESP_OK);
    for(unsigned i=0;i<8;++i) assert(test_led_pixel(i).red==128 && test_led_pixel(i).blue==32);
    ws2812_effect_config_t effect={.effect=WS2812_EFFECT_CHASE,.color={255,0,0},.brightness=255,.period_ms=200};
    assert(ws2812_set_effect(strip,&effect)==ESP_OK);
    ws2812_status_t first,last; ws2812_get_status(strip,&first);
    int64_t end=esp_timer_get_time()+1000000;
    do { pause_ms(5); ws2812_get_status(strip,&last); assert(esp_timer_get_time()<end); }
    while(last.frames_sent<=first.frames_sent);
    assert(last.last_error==ESP_OK && last.effect.effect==WS2812_EFFECT_CHASE);
    assert(ws2812_stop(strip)==ESP_OK);
    for(unsigned i=0;i<8;++i) assert(test_led_pixel(i).red==0);
    assert(ws2812_destroy(strip)==ESP_OK); wait_for_tasks(); assert(!atomic_load(&test_active_strips));
}
static void failures(void)
{
    ws2812_config_t cfg=WS2812_CONFIG_DEFAULT(1,4); ws2812_handle_t strip;
    atomic_store(&test_fail_device,true); assert(ws2812_create(&cfg,&strip)==ESP_FAIL && !strip);
    atomic_store(&test_fail_device,false); atomic_store(&test_fail_refresh,true);
    assert(ws2812_create(&cfg,&strip)==ESP_FAIL && !strip && !atomic_load(&test_active_strips));
    atomic_store(&test_fail_refresh,false); atomic_store(&test_fail_task,true);
    assert(ws2812_create(&cfg,&strip)==ESP_ERR_NO_MEM && !strip && !atomic_load(&test_active_strips));
    atomic_store(&test_fail_task,false); assert(ws2812_create(&cfg,&strip)==ESP_OK);
    atomic_store(&test_fail_refresh,true);
    assert(ws2812_set_color(strip,(ws2812_color_t){1,2,3},255)==ESP_FAIL);
    ws2812_status_t status; ws2812_get_status(strip,&status); assert(status.last_error==ESP_FAIL);
    assert(ws2812_destroy(strip)==ESP_FAIL); wait_for_tasks();
    assert(ws2812_stop(strip)==ESP_ERR_INVALID_STATE);
    atomic_store(&test_fail_refresh,false); assert(ws2812_destroy(strip)==ESP_OK);
    assert(ws2812_create(&cfg,&strip)==ESP_OK && ws2812_destroy(strip)==ESP_OK); wait_for_tasks();
}
static void validation(void)
{
    ws2812_config_t cfg=WS2812_CONFIG_DEFAULT(1,8); ws2812_handle_t strip, other;
    cfg.led_count=0; assert(ws2812_create(&cfg,&strip)==ESP_ERR_INVALID_ARG);
    cfg.led_count=8; cfg.gpio=19; assert(ws2812_create(&cfg,&strip)==ESP_ERR_INVALID_ARG);
    cfg.gpio=1; assert(ws2812_create(&cfg,&strip)==ESP_OK);
    assert(ws2812_create(&cfg,&other)==ESP_ERR_INVALID_STATE && !other);
    ws2812_effect_config_t effect={.effect=WS2812_EFFECT_BREATH,.period_ms=0};
    assert(ws2812_set_effect(strip,&effect)==ESP_ERR_INVALID_ARG);
    effect.period_ms=1000; effect.tail_length=9; assert(ws2812_set_effect(strip,&effect)==ESP_ERR_INVALID_ARG);
    assert(ws2812_destroy(strip)==ESP_OK); wait_for_tasks();
}
int main(int argc,char **argv)
{
    assert(argc==2);
    if(!strcmp(argv[1],"effects")) effects();
    else if(!strcmp(argv[1],"worker")) worker();
    else if(!strcmp(argv[1],"failures")) failures();
    else if(!strcmp(argv[1],"validation")) validation();
    else return 1;
    puts(argv[1]); return 0;
}
