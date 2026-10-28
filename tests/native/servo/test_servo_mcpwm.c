#include <stddef.h>
#include "servo.h"
#include "peripheral_resources.h"
#include "board.h"
#include "board_definition.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct mock_timer { int group; bool enabled,running; };
struct mock_operator { int group,children; mcpwm_timer_handle_t timer; };
struct mock_comparator { mcpwm_oper_handle_t op; uint32_t ticks; };
struct mock_generator { mcpwm_oper_handle_t op; int force; };
static int timers[2],operators[2],calls,fail_at;
static bool fail_compare;
static mcpwm_gen_handle_t last_gen;
static bool fail(void) { return ++calls==fail_at; }
const board_config_t *board_get_config(void) { return &board_definition; }
int64_t esp_timer_get_time(void) { struct timespec n;clock_gettime(CLOCK_MONOTONIC,&n);return (int64_t)n.tv_sec*1000000+n.tv_nsec/1000; }
void vTaskDelay(TickType_t t) { struct timespec n={t/1000,(t%1000)*1000000L};nanosleep(&n,NULL); }
esp_err_t gpio_reset_pin(int p) { (void)p;return ESP_OK; }
esp_err_t gpio_set_level(int p,uint32_t v) { (void)p;assert(v==0);return ESP_OK; }
esp_err_t gpio_set_pull_mode(int p,gpio_pull_mode_t m) { (void)p;assert(m==GPIO_FLOATING);return ESP_OK; }
esp_err_t gpio_input_enable(int p) { (void)p;return ESP_OK; }
esp_err_t mcpwm_new_timer(const mcpwm_timer_config_t *c,mcpwm_timer_handle_t *out) {
    if(fail()) { return ESP_FAIL; }
    assert(c->group_id>=0 && c->group_id<2 && c->resolution_hz==1000000 && c->period_ticks==20000);
    if(timers[c->group_id]==3) { return ESP_ERR_NOT_FOUND; }
    *out=calloc(1,sizeof(**out));(*out)->group=c->group_id;++timers[c->group_id];return ESP_OK;
}
esp_err_t mcpwm_del_timer(mcpwm_timer_handle_t t) { assert(!t->enabled && !t->running);--timers[t->group];free(t);return ESP_OK; }
esp_err_t mcpwm_new_operator(const mcpwm_operator_config_t *c,mcpwm_oper_handle_t *out) {
    if(fail()) { return ESP_FAIL; }
    if(operators[c->group_id]==3) { return ESP_ERR_NOT_FOUND; }
    *out=calloc(1,sizeof(**out));(*out)->group=c->group_id;++operators[c->group_id];return ESP_OK;
}
esp_err_t mcpwm_del_operator(mcpwm_oper_handle_t o) { assert(!o->children);--operators[o->group];free(o);return ESP_OK; }
esp_err_t mcpwm_operator_connect_timer(mcpwm_oper_handle_t o,mcpwm_timer_handle_t t) {
    if(fail()) { return ESP_FAIL; }assert(o->group==t->group);o->timer=t;return ESP_OK;
}
esp_err_t mcpwm_new_comparator(mcpwm_oper_handle_t o,const mcpwm_comparator_config_t *c,mcpwm_cmpr_handle_t *out) {
    if(fail()) { return ESP_FAIL; }assert(c->flags.update_cmp_on_tez);*out=calloc(1,sizeof(**out));(*out)->op=o;++o->children;return ESP_OK;
}
esp_err_t mcpwm_del_comparator(mcpwm_cmpr_handle_t c) { --c->op->children;free(c);return ESP_OK; }
esp_err_t mcpwm_new_generator(mcpwm_oper_handle_t o,const mcpwm_generator_config_t *c,mcpwm_gen_handle_t *out) {
    if(fail()) { return ESP_FAIL; }assert(GPIO_IS_VALID_OUTPUT_GPIO(c->gen_gpio_num));*out=calloc(1,sizeof(**out));(*out)->op=o;++o->children;last_gen=*out;return ESP_OK;
}
esp_err_t mcpwm_del_generator(mcpwm_gen_handle_t g) { --g->op->children;if(last_gen==g)last_gen=NULL;free(g);return ESP_OK; }
esp_err_t mcpwm_generator_set_force_level(mcpwm_gen_handle_t g,int v,bool hold) {
    if(fail()) { return ESP_FAIL; }assert(hold && v>=-1 && v<=1);g->force=v;return ESP_OK;
}
esp_err_t mcpwm_comparator_set_compare_value(mcpwm_cmpr_handle_t c,uint32_t v) {
    if(fail() || fail_compare) { return ESP_FAIL; }assert(v>=500 && v<=2500);c->ticks=v;return ESP_OK;
}
esp_err_t mcpwm_generator_set_action_on_timer_event(mcpwm_gen_handle_t g,mcpwm_gen_timer_event_action_t a) {
    (void)g;if(fail()) { return ESP_FAIL; }assert(a.event==MCPWM_TIMER_EVENT_EMPTY && a.action==MCPWM_GEN_ACTION_HIGH);return ESP_OK;
}
esp_err_t mcpwm_generator_set_action_on_compare_event(mcpwm_gen_handle_t g,mcpwm_gen_compare_event_action_t a) {
    (void)g;if(fail()) { return ESP_FAIL; }assert(a.comparator && a.action==MCPWM_GEN_ACTION_LOW);return ESP_OK;
}
esp_err_t mcpwm_timer_enable(mcpwm_timer_handle_t t) { if(fail()) { return ESP_FAIL; }t->enabled=true;return ESP_OK; }
esp_err_t mcpwm_timer_disable(mcpwm_timer_handle_t t) { assert(t->enabled && !t->running);t->enabled=false;return ESP_OK; }
esp_err_t mcpwm_timer_start_stop(mcpwm_timer_handle_t t,int command) {
    if(fail()) { return ESP_FAIL; }assert(t->enabled);t->running=command==MCPWM_TIMER_START_NO_STOP;return ESP_OK;
}
static servo_pwm_status_t pwm(servo_handle_t s) { servo_pwm_status_t p;assert(servo_get_pwm_status(s,&p)==ESP_OK);return p; }
static servo_handle_t create(int pin) { servo_config_t c=SERVO_CONFIG_DEFAULT(pin,1,1);c.reversed=true;servo_handle_t s;assert(servo_create(&c,&s)==ESP_OK);return s; }
static void empty(void) { assert(!timers[0] && !timers[1] && !operators[0] && !operators[1]); }
static void basics(void) {
    servo_handle_t s=create(14);assert(last_gen->force==0 && !pwm(s).output_enabled);
    assert(!strcmp(pwm(s).backend,"MCPWM") && !pwm(s).hardware_readback && pwm(s).period_ticks==20000);
    assert(servo_set_angle(s,45)==ESP_OK && pwm(s).compare_ticks==2000 && last_gen->force==-1);
    assert(servo_set_pulse_us(s,1000)==ESP_OK && pwm(s).compare_ticks==1000);
    fail_compare=true;assert(servo_set_angle(s,90)==ESP_FAIL && pwm(s).compare_ticks==1000);fail_compare=false;
    assert(servo_stop(s)==ESP_OK && last_gen->force==0 && !pwm(s).output_enabled);
    assert(servo_set_angle(s,90)==ESP_OK && last_gen->force==-1 && pwm(s).compare_ticks==1500);
    assert(servo_record_home(s)==ESP_OK);
    assert(servo_move_by(s,45,30,SERVO_CURVE_SMOOTH)==ESP_OK);
    assert(servo_wait_until_idle(s,500)==ESP_OK && pwm(s).compare_ticks==1000);
    assert(servo_return_home(s,30,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(s,500)==ESP_OK && pwm(s).compare_ticks==1500);
    assert(servo_destroy(s)==ESP_OK);empty();
}
static void failures(void) {
    for(int step=1;step<=11;step++) {
        calls=0;fail_at=step;servo_config_t c=SERVO_CONFIG_DEFAULT(14,1,1);servo_handle_t s;
        assert(servo_create(&c,&s)==ESP_FAIL && !s);empty();
        fail_at=0;calls=0;s=create(14);assert(servo_destroy(s)==ESP_OK);empty();
    }
}
static void resources(void) {
    int pins[]={4,13,14,18,19,21};servo_handle_t s[6];
    for(unsigned i=0;i<6;i++)s[i]=create(pins[i]);
    assert(timers[0]==3 && timers[1]==3);
    peripheral_claim_t led={.gpios=1ULL<<32,.ledc_timer=1,.ledc_channel=1,.frequency_hz=50,.resolution_bits=14};
    assert(peripheral_resources_acquire(&led)==ESP_OK);peripheral_resources_release(&led);
    servo_config_t c=SERVO_CONFIG_DEFAULT(22,1,1);servo_handle_t extra;
    assert(servo_create(&c,&extra)==ESP_ERR_NOT_FOUND && !extra);
    for(unsigned i=0;i<6;i++)assert(servo_destroy(s[i])==ESP_OK);
    empty();extra=create(22);assert(servo_destroy(extra)==ESP_OK);empty();
}
static void motion(void) {
    servo_handle_t s=create(14);assert(servo_set_angle(s,45)==ESP_OK);
    int64_t start=esp_timer_get_time();assert(servo_move_to(s,135,500,SERVO_CURVE_SMOOTH)==ESP_OK);
    uint32_t previous=2000;unsigned changes=0;
    while(esp_timer_get_time()-start<560000) {
        uint32_t v=pwm(s).compare_ticks;assert(v<=previous && v>=1000);if(v!=previous)++changes;previous=v;vTaskDelay(10);
    }
    assert(changes>=18 && pwm(s).compare_ticks==1000);
    assert(servo_wait_until_idle(s,0)==ESP_OK);assert(servo_destroy(s)==ESP_OK);empty();
}
static void repeat_motion(void) {
    servo_handle_t s=create(14);assert(servo_set_angle(s,60)==ESP_OK);
    for(unsigned i=0;i<2;i++) {
        int64_t start=esp_timer_get_time();
        assert(servo_move_to(s,i?60:120,500,SERVO_CURVE_SMOOTH)==ESP_OK);
        assert(servo_wait_until_idle(s,750)==ESP_OK);
        int64_t elapsed=esp_timer_get_time()-start;
        assert(elapsed>=480000 && elapsed<=650000);
        assert(pwm(s).compare_ticks==(i?1833:1167) && last_gen->force==-1);
        // Exercise resuming the same worker after it blocks idle, without stopping PWM.
        vTaskDelay(100);
    }
    assert(servo_destroy(s)==ESP_OK);empty();
}
#include "auto_release_cases.h"
static void independent_release(void) {
    servo_config_t config=SERVO_CONFIG_DEFAULT(25,1,1);config.auto_release_ms=60;
    servo_handle_t left,right;assert(servo_create(&config,&left)==ESP_OK);
    mcpwm_gen_handle_t left_gen=last_gen;
    config.gpio=26;config.channel=2;config.auto_release_ms=240;
    assert(servo_create(&config,&right)==ESP_OK);
    mcpwm_gen_handle_t right_gen=last_gen;
    assert(servo_set_angle(left,60)==ESP_OK && servo_set_angle(right,120)==ESP_OK);
    await_release(left);
    assert(left_gen->force==0 && right_gen->force==-1 && pwm(right).output_enabled);
    await_release(right);assert(right_gen->force==0);
    assert(servo_destroy(left)==ESP_OK && servo_destroy(right)==ESP_OK);empty();
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"basics"))basics();else if(!strcmp(argv[1],"failures"))failures();
    else if(!strcmp(argv[1],"resources"))resources();else if(!strcmp(argv[1],"motion"))motion();
    else if(!strcmp(argv[1],"repeat"))repeat_motion();
    else if(!strcmp(argv[1],"auto_release")) { automatic_release();empty(); }
    else if(!strcmp(argv[1],"independent_release"))independent_release();else assert(0);
}
