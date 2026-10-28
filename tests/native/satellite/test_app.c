// Real satellite application, mocked peripheral boundaries; deterministic task stepping.
#include "../../../apps/satellite/main/satellite.c"
#include <assert.h>
#include <stdlib.h>
static int64_t clock_us=1000000;
static bool voltage_fail;
static float battery=4.0f;
static uint8_t ap=1;
static bool connected=true;
struct servo_instance { servo_status_t status; };
static struct servo_instance fake_servo[3];
static unsigned created;
int64_t esp_timer_get_time(void) { return clock_us; }
void vTaskDelay(TickType_t t) { clock_us+=(int64_t)t*1000; }
esp_err_t runtime_register_shutdown_hook(runtime_shutdown_hook_t hook) { (void)hook; return ESP_OK; }
esp_err_t servo_create(const servo_config_t *c,servo_handle_t *out) {
    assert(created<3); *out=&fake_servo[created++];
    (*out)->status.max_angle=c->max_angle; return ESP_OK;
}
esp_err_t servo_get_status(servo_handle_t s,servo_status_t *out) { *out=s->status; return ESP_OK; }
esp_err_t servo_get_pwm_status(servo_handle_t s,servo_pwm_status_t *out) { (void)s;(void)out; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t servo_cancel_motion(servo_handle_t s) { s->status.moving=false; return s->status.last_error; }
esp_err_t servo_stop(servo_handle_t s) { s->status.moving=false; s->status.output_enabled=false; s->status.auto_released=false; return ESP_OK; }
esp_err_t servo_set_angle(servo_handle_t s,float angle) {
    if(angle<0 || angle>s->status.max_angle) return ESP_ERR_INVALID_ARG;
    s->status.angle_known=s->status.output_enabled=true; s->status.moving=s->status.auto_released=false;
    s->status.commanded_angle=s->status.target_angle=angle; s->status.last_error=ESP_OK; return ESP_OK;
}
esp_err_t servo_move_to(servo_handle_t s,float angle,uint32_t duration,servo_curve_t curve) {
    (void)duration; (void)curve; if(!s->status.output_enabled && !s->status.auto_released) return ESP_ERR_INVALID_STATE;
    if(angle<0 || angle>s->status.max_angle) return ESP_ERR_INVALID_ARG;
    s->status.target_angle=angle; s->status.moving=s->status.output_enabled=true;s->status.auto_released=false; return ESP_OK;
}
esp_err_t servo_move_by(servo_handle_t s,float value,uint32_t ms,servo_curve_t c) { return servo_move_to(s,s->status.commanded_angle+value,ms,c); }
esp_err_t servo_record_home(servo_handle_t s) { s->status.home_known=true; s->status.home_angle=s->status.commanded_angle; return ESP_OK; }
esp_err_t servo_return_home(servo_handle_t s,uint32_t ms,servo_curve_t c) { return s->status.home_known?servo_move_to(s,s->status.home_angle,ms,c):ESP_ERR_INVALID_STATE; }
esp_err_t servo_set_limits(servo_handle_t s,float min,float max) { (void)min; s->status.max_angle=max; return ESP_OK; }
esp_err_t servo_sweep(servo_handle_t s,float low,float high,uint32_t ms,uint32_t hold) { (void)hold; (void)high; return servo_move_to(s,low,ms,SERVO_CURVE_SMOOTH); }
esp_err_t voltage_sensor_create(const voltage_sensor_config_t *c,voltage_sensor_handle_t *out) { *out=(voltage_sensor_handle_t)(uintptr_t)(c->gpio+1); return ESP_OK; }
esp_err_t voltage_sensor_read(voltage_sensor_handle_t s,voltage_sensor_sample_t *out) {
    if(voltage_fail) return ESP_ERR_TIMEOUT;
    int gpio=(int)(uintptr_t)s-1;
    *out=(voltage_sensor_sample_t){.voltage_v=gpio==3?battery:gpio>=4?0.1f:5,.timestamp_us=clock_us}; return ESP_OK;
}
esp_err_t wifi_manager_get_signal(wifi_signal_sample_t *out) {
    if(!connected) return ESP_ERR_INVALID_STATE;
    *out=(wifi_signal_sample_t){.connected=true,.rssi_dbm=-50,.bssid={ap},.timestamp_us=clock_us}; return ESP_OK;
}
static void setup(void) {
    s_lock=xSemaphoreCreateMutex(); initialize(); sample(clock_us);
    for(unsigned i=0;i<3;i++) {
        assert(s_status.axis[i].gpio==s_axis_gpio[i]);
        assert(s_status.axis[i].ready && !fake_servo[i].status.output_enabled && !fake_servo[i].status.angle_known);
    }
    for(unsigned i=0;i<3;i++) assert(satellite_axis_command(i,"set",60,1000)==ESP_OK);
    control(clock_us);
}
static void ownership(void) {
    assert(satellite_axis_command(0,"auto",0,1000)==ESP_OK); control(clock_us);
    assert(fake_servo[0].status.moving);
    assert(satellite_axis_command(0,"record",0,1000)==ESP_ERR_INVALID_STATE);
    assert(s_status.axis[0].mode==SAT_MODE_AUTO);
    assert(satellite_axis_command(0,"typo",0,1000)==ESP_ERR_INVALID_ARG && s_status.axis[0].mode==SAT_MODE_AUTO);
    assert(satellite_axis_command(0,"by",10,1000)==ESP_OK);
    assert(s_status.axis[0].mode==SAT_MODE_MANUAL && !angle_search_active(&s_axes[0].search));
    float target=fake_servo[0].status.target_angle; control(clock_us+100000); assert(fake_servo[0].status.target_angle==target);
}
static void policy(void) {
    assert(satellite_axis_command(1,"auto",0,1000)==ESP_OK); control(clock_us);
    battery=3.1f; clock_us+=1000000; sample(clock_us); control(clock_us);
    assert(s_status.low_battery && !fake_servo[1].status.moving && s_status.axis[1].mode==SAT_MODE_MANUAL);
    assert(satellite_axis_command(0,"move",90,1000)==ESP_ERR_INVALID_STATE);
    battery=3.5f; clock_us+=1000000; sample(clock_us); assert(s_status.low_battery);
    battery=3.8f; clock_us+=1000000; sample(clock_us); assert(!s_status.low_battery);
    assert(s_status.axis[1].mode==SAT_MODE_MANUAL); // Recovery does not unexpectedly restart automation.
    voltage_fail=true; clock_us+=1000000; sample(clock_us); control(clock_us);
    assert(s_status.power_unknown && !s_status.power[0].power_valid);
    assert(satellite_axis_command(0,"auto",0,1000)==ESP_ERR_INVALID_STATE);
}
static void bssid_and_standby(void) {
    assert(satellite_axis_command(0,"auto",0,1000)==ESP_OK); control(clock_us);
    ap=2; clock_us+=1000000; sample(clock_us); control(clock_us);
    assert(s_status.axis[0].mode==SAT_MODE_MANUAL && s_status.axis[0].error==ESP_ERR_INVALID_STATE);
    assert(satellite_command("standby",0,0)==ESP_OK);
    for(unsigned i=0;i<3;i++) assert(!fake_servo[i].status.output_enabled);
    assert(satellite_axis_command(0,"set",60,1000)==ESP_ERR_INVALID_STATE);
    assert(satellite_command("resume",0,0)==ESP_OK);
    assert(!fake_servo[0].status.output_enabled); // Resume only permits future explicit motion.
}
static void once(void) {
    assert(satellite_axis_command(0,"scan",0,1000)==ESP_OK);
    for(unsigned n=0;n<500 && s_status.axis[0].mode==SAT_MODE_SCAN;n++) {
        fake_servo[0].status.commanded_angle=fake_servo[0].status.target_angle;
        fake_servo[0].status.moving=false;
        clock_us+=1000000; sample(clock_us); control(clock_us);
    }
    assert(s_status.axis[0].mode==SAT_MODE_MANUAL && s_axes[0].search.phase==ANGLE_SEARCH_DONE);
    angle_search_point_t points[ANGLE_SEARCH_MAX_POINTS]; size_t count=0;
    assert(satellite_get_scan(0,points,ANGLE_SEARCH_MAX_POINTS,&count)==ESP_OK && count>19);
    assert(satellite_get_scan(0,points,1,&count)==ESP_ERR_INVALID_SIZE);
    assert(points[0].angle==0 && points[0].score==-50);
    clock_us+=1000000000; control(clock_us); assert(!fake_servo[0].status.moving);
    s_current[0]=NULL; clock_us+=1000000; sample(clock_us);
    assert(s_status.power[0].voltage_valid && !s_status.power[0].power_valid);
    assert(satellite_axis_command(1,"auto",0,1000)==ESP_ERR_INVALID_STATE);
}
static void unified_angle(void) {
    servo_status_t *s=&fake_servo[1].status;
    s->output_enabled=s->angle_known=false;
    assert(satellite_axis_command(1,"angle",45,500)==ESP_OK);
    assert(s->commanded_angle==45 && !s->moving && s->output_enabled);
    s->output_enabled=false;s->auto_released=true;
    assert(satellite_axis_command(1,"angle",135,500)==ESP_OK);
    assert(s->target_angle==135 && s->moving && s->output_enabled && !s->auto_released);
    assert(satellite_axis_command(1,"stop",0,500)==ESP_OK);
    assert(satellite_axis_command(1,"angle",90,500)==ESP_OK);
    assert(s->commanded_angle==90 && !s->moving && s->output_enabled);
    assert(satellite_axis_command(1,"angle",181,500)==ESP_ERR_INVALID_ARG);
    s->last_error=ESP_FAIL;
    assert(satellite_axis_command(1,"angle",60,500)==ESP_OK && s->last_error==ESP_OK);
}
int main(int argc,char **argv) {
    assert(argc==2); setup();
    if(!strcmp(argv[1],"ownership"))ownership(); else if(!strcmp(argv[1],"policy"))policy();
    else if(!strcmp(argv[1],"standby"))bssid_and_standby();
    else if(!strcmp(argv[1],"once"))once();
    else if(!strcmp(argv[1],"angle"))unified_angle(); else assert(0);
}
