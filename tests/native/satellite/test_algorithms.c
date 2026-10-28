#include "angle_search.h"
#include "power_monitor.h"
#include <assert.h>
#include <math.h>
#include <string.h>
static angle_search_config_t config(void) {
    return (angle_search_config_t){.min_angle=0,.max_angle=100,.coarse_step=20,.fine_step=5,.min_score=-100,.improvement=0,.samples=3,.settle_ms=100,.timeout_ms=3000};
}
static void converge(void) {
    angle_search_t s; angle_search_config_t c=config(); assert(angle_search_start(&s,&c)==ESP_OK);
    float position=0,target; int64_t now=0; unsigned moves=0;
    while(angle_search_active(&s) && now<100000000) {
        now+=100000;
        float score=-30-(position-65)*(position-65)*0.01f;
        if(angle_search_tick(&s,now,false,true,score,now,&target)) { assert(target>=0 && target<=100); position=target; moves++; }
    }
    assert(s.phase==ANGLE_SEARCH_DONE && fabsf(position-65)<0.01f && moves>7);
    assert(s.point_count==moves-1 && s.points[0].angle==0);
}
static void validity(void) {
    angle_search_t s; angle_search_config_t c=config(); float target;
    assert(angle_search_start(&s,&c)==ESP_OK);
    assert(angle_search_tick(&s,0,false,false,0,0,&target));
    angle_search_tick(&s,100000,false,true,-40,100000,&target);
    angle_search_tick(&s,300000,false,true,-40,300000,&target);
    assert(s.count==1);
    angle_search_tick(&s,400000,false,true,-40,300000,&target); assert(s.count==1); // Duplicate sample.
    angle_search_tick(&s,500000,false,true,NAN,500000,&target); assert(s.count==1);
    angle_search_tick(&s,3100000,false,false,0,0,&target); assert(s.phase==ANGLE_SEARCH_FAILED && s.error==ESP_ERR_TIMEOUT);
    c.fine_step=0; assert(angle_search_start(&s,&c)==ESP_ERR_INVALID_ARG);
    c=config(); c.max_angle=INFINITY; assert(angle_search_start(&s,&c)==ESP_ERR_INVALID_ARG);
}
static void weak(void) {
    angle_search_t s; angle_search_config_t c=config(); c.min_score=0.01f;
    assert(angle_search_start(&s,&c)==ESP_OK); float target;
    for(int64_t now=0;angle_search_active(&s) && now<100000000;now+=100000) angle_search_tick(&s,now,false,true,0,now,&target);
    assert(s.phase==ANGLE_SEARCH_FAILED && s.error==ESP_ERR_NOT_FOUND);
    angle_search_cancel(&s); assert(!angle_search_active(&s));
}
static void power(void) {
    power_monitor_t s={0};
    assert(power_monitor_update(&s,1000000,true,5,true,0.2f,3000)==ESP_OK);
    assert(s.power_valid && fabsf(s.power_w-1)<0.0001 && s.energy_wh==0);
    power_monitor_update(&s,2000000,true,5,true,0.2f,3000);
    assert(fabs(s.energy_wh-1.0/3600)<0.000001);
    double old=s.energy_wh;
    power_monitor_update(&s,3000000,true,5,false,0,3000); assert(!s.power_valid && s.voltage_valid);
    power_monitor_update(&s,4000000,true,5,true,0.2f,3000); assert(s.energy_wh==old);
    power_monitor_update(&s,20000000,true,5,true,0.2f,3000); assert(s.energy_wh==old); // No integrating across stale gaps.
    assert(power_monitor_update(&s,19000000,true,5,true,0.2f,3000)==ESP_ERR_INVALID_ARG);
    power_monitor_update(&s,21000000,true,NAN,true,1,3000); assert(!s.voltage_valid && !s.power_valid);
    assert(power_monitor_low(false,3.2,3.3,3.6)); assert(power_monitor_low(true,3.5,3.3,3.6));
    assert(!power_monitor_low(true,3.7,3.3,3.6)); assert(!power_monitor_low(false,3.5,3.3,3.6));
}
int main(int argc,char **argv) {
    assert(argc==2);
    if(!strcmp(argv[1],"converge"))converge(); else if(!strcmp(argv[1],"validity"))validity();
    else if(!strcmp(argv[1],"weak"))weak(); else if(!strcmp(argv[1],"power"))power(); else assert(0);
}
