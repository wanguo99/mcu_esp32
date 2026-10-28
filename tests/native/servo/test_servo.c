#include "servo.h"
#include "board.h"
#include "board_definition.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

extern atomic_bool test_fail_task;
static atomic_bool fail_pwm, fail_update;
static atomic_uint duty, pending_duty;
static bool pad_input[49];
static gpio_pull_mode_t pad_pull[49];
static bool fail_pad;
const board_config_t *board_get_config(void) { return &board_definition; }
int64_t esp_timer_get_time(void) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
    return (int64_t)now.tv_sec*1000000+now.tv_nsec/1000;
}
void vTaskDelay(TickType_t ticks) {
    struct timespec delay={ticks/1000,(ticks%1000)*1000000L}; nanosleep(&delay,NULL);
}
esp_err_t gpio_reset_pin(int gpio) { pad_input[gpio]=false; pad_pull[gpio]=GPIO_PULLUP_ONLY; return ESP_OK; }
esp_err_t gpio_set_level(int gpio,uint32_t level) { (void)gpio; assert(level==0); return ESP_OK; }
esp_err_t gpio_set_pull_mode(int gpio,gpio_pull_mode_t mode) {
    if(fail_pad) return ESP_FAIL;
    pad_pull[gpio]=mode; return ESP_OK;
}
esp_err_t gpio_input_enable(int gpio) { pad_input[gpio]=true; return ESP_OK; }
esp_err_t ledc_timer_config(const ledc_timer_config_t *c) { (void)c; return ESP_OK; }
esp_err_t ledc_channel_config(const ledc_channel_config_t *c) {
    assert(pad_input[c->gpio_num] && pad_pull[c->gpio_num]==GPIO_FLOATING);
    pending_duty=duty=c->duty; return ESP_OK;
}
esp_err_t ledc_set_duty_and_update(int m,int c,uint32_t d,uint32_t h) {
    (void)m;(void)c;(void)d;(void)h; return ESP_FAIL; // No global fade service installed.
}
esp_err_t ledc_set_duty(int m,int c,uint32_t d) {
    (void)m;(void)c; if(fail_pwm) return ESP_FAIL; pending_duty=d; return ESP_OK;
}
esp_err_t ledc_update_duty(int m,int c) {
    (void)m;(void)c; if(fail_update) return ESP_FAIL; duty=pending_duty; return ESP_OK;
}
esp_err_t ledc_stop(int m,int c,uint32_t idle) { (void)m;(void)c; duty=idle; return ESP_OK; }
uint32_t ledc_get_freq(int m,int t) { (void)m;(void)t; return 50; }
uint32_t ledc_get_duty(int m,int c) { (void)m;(void)c; return duty; }
static servo_handle_t create(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1); servo_handle_t handle;
    assert(servo_create(&config,&handle)==ESP_OK); return handle;
}
static servo_status_t status(servo_handle_t s) {
    servo_status_t out; assert(servo_get_status(s,&out)==ESP_OK); return out;
}
static void basics(void) {
    servo_handle_t s=create(); assert(!status(s).output_enabled && !status(s).angle_known);
    servo_pwm_status_t p; assert(servo_get_pwm_status(s,&p)==ESP_OK);
    assert(!strcmp(p.backend,"LEDC") && p.hardware_readback && p.period_ticks==16384 && !p.output_enabled);
    assert(servo_move_to(s,90,100,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(s,NAN)==ESP_ERR_INVALID_ARG);
    assert(servo_set_angle(s,181)==ESP_ERR_INVALID_ARG);
    assert(servo_set_pulse_us(s,499)==ESP_ERR_INVALID_ARG);
    assert(servo_set_pulse_us(s,1500)==ESP_OK); assert(status(s).commanded_angle==90);
    assert(servo_stop(s)==ESP_OK && duty==0);
    assert(servo_move_to(s,0,100,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(s,0)==ESP_OK);
    assert(servo_destroy(s)==ESP_OK); s=create(); assert(servo_destroy(s)==ESP_OK);
}
static void motion(void) {
    servo_handle_t s=create(); assert(servo_set_angle(s,0)==ESP_OK);
    assert(servo_move_to(s,180,200,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(s,0)==ESP_ERR_TIMEOUT);
    assert(servo_wait_until_idle(s,1000)==ESP_OK);
    assert(status(s).commanded_angle==180 && status(s).output_enabled);
    assert(servo_move_to(s,0,100,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK); assert(status(s).commanded_angle==0);
    assert(servo_move_with_limits(s,90,90,360)==ESP_OK);
    assert(status(s).step_remaining_ms>1400 && status(s).step_remaining_ms<=1500);
    assert(servo_cancel_motion(s)==ESP_OK); assert(!status(s).moving && status(s).output_enabled);
    assert(servo_move_with_limits(s,90,0,360)==ESP_ERR_INVALID_ARG);
    assert(servo_destroy(s)==ESP_OK);
}
static void sequence(void) {
    servo_handle_t s=create(); assert(servo_set_angle(s,0)==ESP_OK);
    servo_step_t steps[]={{60,30,20},{120,30,0}};
    assert(servo_play_sequence(s,steps,2,2,SERVO_CURVE_SMOOTH)==ESP_OK);
    steps[1].angle=10; // The driver owns a copy, not the caller's array.
    assert(servo_wait_until_idle(s,1000)==ESP_OK); assert(status(s).commanded_angle==120);
    assert(servo_sweep(s,30,150,30,0)==ESP_OK);
    assert(servo_wait_until_idle(s,150)==ESP_ERR_TIMEOUT);
    assert(servo_stop(s)==ESP_OK); assert(!status(s).moving && !status(s).output_enabled);
    assert(servo_set_angle(s,0)==ESP_OK);
    assert(servo_sweep(s,30,150,30,0)==ESP_OK); assert(servo_destroy(s)==ESP_OK);
}
static void failures(void) {
    servo_handle_t s=create(); assert(servo_set_angle(s,0)==ESP_OK);
    test_fail_task=true; assert(servo_move_to(s,90,100,SERVO_CURVE_LINEAR)==ESP_ERR_NO_MEM);
    assert(!status(s).moving); test_fail_task=false;
    assert(servo_move_to(s,90,100,SERVO_CURVE_LINEAR)==ESP_OK);
    fail_pwm=true; assert(servo_wait_until_idle(s,1000)==ESP_FAIL);
    assert(!status(s).moving && status(s).last_error==ESP_FAIL);
    fail_pwm=false; assert(servo_set_angle(s,0)==ESP_OK);
    assert(servo_move_to(s,90,50,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK); assert(servo_destroy(s)==ESP_OK);
}
static void relative_home(void) {
    servo_handle_t s=create();
    assert(servo_move_by(s,30,50,SERVO_CURVE_SMOOTH)==ESP_ERR_INVALID_STATE);
    assert(servo_record_home(s)==ESP_ERR_INVALID_STATE);
    assert(servo_return_home(s,50,SERVO_CURVE_SMOOTH)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(s,60)==ESP_OK); assert(servo_record_home(s)==ESP_OK);
    assert(status(s).home_known && status(s).home_angle==60);
    assert(servo_move_by(s,30,50,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK && status(s).commanded_angle==90);
    assert(servo_move_by(s,-20,0,SERVO_CURVE_LINEAR)==ESP_OK && status(s).commanded_angle==70);
    assert(servo_move_by(s,NAN,50,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_ARG);
    assert(servo_move_by(s,200,50,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_ARG);
    assert(status(s).commanded_angle==70 && !status(s).moving);
    assert(servo_return_home(s,50,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK && status(s).commanded_angle==60);
    assert(servo_move_to(s,180,10000,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_record_home(s)==ESP_ERR_INVALID_STATE); // No unstable home capture.
    assert(servo_move_by(s,5,50,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(status(s).target_angle<100); // Based on current command, not pending 180-degree target.
    assert(servo_wait_until_idle(s,1000)==ESP_OK);
    assert(servo_stop(s)==ESP_OK);
    assert(servo_move_by(s,5,50,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(servo_return_home(s,50,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(status(s).home_angle==60); assert(servo_destroy(s)==ESP_OK);
    s=create(); assert(!status(s).home_known); assert(servo_destroy(s)==ESP_OK);
}
static void limits(void) {
    servo_handle_t s=create(); assert(servo_set_angle(s,60)==ESP_OK);
    unsigned calibrated_duty=duty;
    assert(servo_record_home(s)==ESP_OK);
    assert(servo_set_limits(s,0,120)==ESP_OK);
    assert(servo_set_angle(s,60)==ESP_OK && duty==calibrated_duty); // No pulse remapping.
    assert(servo_set_angle(s,121)==ESP_ERR_INVALID_ARG);
    assert(servo_set_pulse_us(s,2500)==ESP_ERR_INVALID_ARG);
    assert(servo_move_with_limits(s,130,60,120)==ESP_ERR_INVALID_ARG);
    servo_step_t bad[]={{80,50,0},{130,50,0}};
    assert(servo_play_sequence(s,bad,2,1,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_ARG);
    assert(!status(s).moving);
    assert(servo_set_limits(s,0,50)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(s,40)==ESP_OK);
    assert(servo_set_limits(s,0,50)==ESP_ERR_INVALID_STATE); // Saved home excluded.
    assert(servo_record_home(s)==ESP_OK);
    assert(servo_set_limits(s,0,50)==ESP_OK);
    assert(servo_move_by(s,11,50,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_ARG);
    assert(servo_move_to(s,45,500,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_set_limits(s,0,180)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(s,90)==ESP_ERR_INVALID_ARG && status(s).moving);
    assert(servo_cancel_motion(s)==ESP_OK);
    assert(servo_set_limits(s,0,180)==ESP_OK);
    assert(servo_set_limits(s,0,181)==ESP_ERR_INVALID_ARG);
    assert(servo_set_limits(s,90,90)==ESP_ERR_INVALID_ARG);
    assert(servo_set_limits(s,NAN,90)==ESP_ERR_INVALID_ARG);
    assert(servo_destroy(s)==ESP_OK);
}
static void pwm_recovery(void) {
    servo_handle_t s=create();
    assert(servo_set_angle(s,90)==ESP_OK && duty==1229);
    fail_update=true;
    assert(servo_set_angle(s,45)==ESP_FAIL && duty==1229);
    assert(status(s).commanded_angle==90 && status(s).target_angle==90 && status(s).last_error==ESP_FAIL);
    fail_update=false;
    assert(servo_set_angle(s,45)==ESP_OK && status(s).commanded_angle==45);
    assert(servo_stop(s)==ESP_OK && duty==0);
    assert(servo_set_angle(s,135)==ESP_OK && status(s).output_enabled);
    assert(servo_destroy(s)==ESP_OK);
}
static void pad_setup(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1); servo_handle_t s;
    fail_pad=true;
    assert(servo_create(&config,&s)==ESP_FAIL && !s);
    fail_pad=false;
    // A failed pad setup must release its claim so creating the same IO works.
    assert(servo_create(&config,&s)==ESP_OK);
    assert(pad_input[1] && pad_pull[1]==GPIO_FLOATING && duty==0);
    assert(servo_set_angle(s,45)==ESP_OK && duty==819);
    assert(servo_destroy(s)==ESP_OK);
    assert(!pad_input[1] && pad_pull[1]==GPIO_PULLUP_ONLY);
}
static void reversed_direction(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1); servo_handle_t s;
    config.reversed=true;
    assert(servo_create(&config,&s)==ESP_OK);
    assert(servo_set_angle(s,0)==ESP_OK && duty==2048);
    assert(servo_set_angle(s,45)==ESP_OK && duty==1638);
    assert(servo_set_angle(s,90)==ESP_OK && duty==1229);
    assert(servo_set_angle(s,135)==ESP_OK && duty==819);
    assert(servo_set_angle(s,180)==ESP_OK && duty==410);
    assert(servo_set_pulse_us(s,2000)==ESP_OK && status(s).commanded_angle==45 && duty==1638);
    assert(servo_move_by(s,45,0,SERVO_CURVE_LINEAR)==ESP_OK && duty==1229);
    assert(servo_record_home(s)==ESP_OK);
    assert(servo_move_to(s,135,40,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK && duty==819);
    assert(servo_return_home(s,40,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(s,1000)==ESP_OK && duty==1229 && status(s).commanded_angle==90);
    assert(servo_set_limits(s,45,135)==ESP_OK);
    assert(servo_set_pulse_us(s,2500)==ESP_ERR_INVALID_ARG);
    assert(servo_destroy(s)==ESP_OK);
}
static void half_second_motion(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1); servo_handle_t s;
    config.reversed=true;
    assert(servo_create(&config,&s)==ESP_OK);
    assert(servo_set_angle(s,45)==ESP_OK && duty==1638);
    int64_t start=esp_timer_get_time();
    assert(servo_move_to(s,135,500,SERVO_CURVE_SMOOTH)==ESP_OK);
    unsigned previous=duty,changes=0;
    while(status(s).moving) {
        unsigned current=duty;
        assert(current<=previous && current>=819);
        if(current!=previous) ++changes;
        previous=current;
        assert(esp_timer_get_time()-start<800000);
        vTaskDelay(10);
    }
    int64_t elapsed=esp_timer_get_time()-start;
    assert(elapsed>=480000 && elapsed<650000 && changes>=10);
    assert(duty==819 && status(s).commanded_angle==135);
    assert(servo_destroy(s)==ESP_OK);
}
#include "auto_release_cases.h"
static void release_task_failure(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1);config.auto_release_ms=80;
    servo_handle_t s;assert(servo_create(&config,&s)==ESP_OK);
    test_fail_task=true;
    assert(servo_set_angle(s,90)==ESP_ERR_NO_MEM && !status(s).output_enabled && duty==0);
    test_fail_task=false;
    assert(servo_set_angle(s,90)==ESP_OK);await_release(s);
    assert(servo_set_angle(s,90)==ESP_OK);
    fail_pwm=true;assert(servo_set_angle(s,120)==ESP_FAIL);fail_pwm=false;
    await_release(s);assert(status(s).last_error==ESP_FAIL);
    assert(servo_set_angle(s,90)==ESP_OK && status(s).last_error==ESP_OK);
    assert(servo_destroy(s)==ESP_OK);
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"basics")) basics(); else if(!strcmp(argv[1],"motion")) motion();
    else if(!strcmp(argv[1],"sequence")) sequence(); else if(!strcmp(argv[1],"failures")) failures();
    else if(!strcmp(argv[1],"relative_home")) relative_home();
    else if(!strcmp(argv[1],"limits")) limits();
    else if(!strcmp(argv[1],"pwm_recovery")) pwm_recovery();
    else if(!strcmp(argv[1],"pad_setup")) pad_setup();
    else if(!strcmp(argv[1],"reversed_direction")) reversed_direction();
    else if(!strcmp(argv[1],"half_second_motion")) half_second_motion();
    else if(!strcmp(argv[1],"auto_release")) { automatic_release();assert(duty==0); }
    else if(!strcmp(argv[1],"release_task_failure")) release_task_failure(); else assert(0);
}
