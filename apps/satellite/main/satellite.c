#include "satellite.h"
#include "sdkconfig.h"
#include "angle_search.h"
#include "runtime.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#if CONFIG_MCU_ENABLE_SERVO
#include "servo.h"
#endif
#if CONFIG_MCU_ENABLE_VOLTAGE_SENSOR
#include "voltage_sensor.h"
#endif
#if CONFIG_MCU_ENABLE_NETWORK
#include "wifi_manager.h"
#endif
#if CONFIG_MCU_ENABLE_MPU6050
#include "mpu6050.h"
#endif
#if CONFIG_MCU_ENABLE_WS2812
#include "ws2812.h"
#endif
#if CONFIG_MCU_ENABLE_FAN
#include "fan.h"
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
#include "relay_uart.h"
#endif
static const char *TAG="satellite";
static SemaphoreHandle_t s_lock;
static satellite_status_t s_status;
static bool s_standby;
static struct {
#if CONFIG_MCU_ENABLE_SERVO
    servo_handle_t servo;
#endif
    angle_search_t search;
    int64_t next_search_us;
    uint8_t bssid[6];
} s_axes[SATELLITE_AXES];
#if CONFIG_MCU_ENABLE_VOLTAGE_SENSOR
static voltage_sensor_handle_t s_voltage[4],s_current[4];
#endif
#if CONFIG_MCU_ENABLE_MPU6050
static mpu6050_handle_t s_imu;
#endif
#if CONFIG_MCU_ENABLE_WS2812
static ws2812_handle_t s_led;
static int s_led_state=-1;
static uint8_t s_brightness=24;
#endif
#if CONFIG_MCU_ENABLE_FAN
static fan_handle_t s_fan;
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
static relay_uart_handle_t s_relay;
#endif
#if CONFIG_MCU_ENABLE_MPU6050
static unsigned s_tilt_count;
#endif
static const int s_axis_gpio[]={CONFIG_SAT_AXIS0_GPIO,CONFIG_SAT_AXIS1_GPIO,CONFIG_SAT_AXIS2_GPIO};
static const int s_voltage_gpio[]={CONFIG_SAT_MON0_V_GPIO,CONFIG_SAT_MON1_V_GPIO,CONFIG_SAT_MON2_V_GPIO,CONFIG_SAT_MON3_V_GPIO};
#if CONFIG_MCU_ENABLE_VOLTAGE_SENSOR
static const int s_current_gpio[]={CONFIG_SAT_MON0_I_GPIO,CONFIG_SAT_MON1_I_GPIO,CONFIG_SAT_MON2_I_GPIO,CONFIG_SAT_MON3_I_GPIO};
static const int s_zero_mv[]={CONFIG_SAT_MON0_ZERO_MV,CONFIG_SAT_MON1_ZERO_MV,CONFIG_SAT_MON2_ZERO_MV,CONFIG_SAT_MON3_ZERO_MV};
static const int s_mv_per_a[]={CONFIG_SAT_MON0_MV_PER_A,CONFIG_SAT_MON1_MV_PER_A,CONFIG_SAT_MON2_MV_PER_A,CONFIG_SAT_MON3_MV_PER_A};
#endif
static __attribute__((unused)) bool blocked(void) { return s_standby || s_status.low_battery || s_status.power_unknown || s_status.pose_fault; }

static esp_err_t stop_axis(unsigned i,bool disable)
{
    angle_search_cancel(&s_axes[i].search); s_status.axis[i].mode=SAT_MODE_MANUAL;
#if CONFIG_MCU_ENABLE_SERVO
    if(s_axes[i].servo) return disable?servo_stop(s_axes[i].servo):servo_cancel_motion(s_axes[i].servo);
#else
    (void)disable;
#endif
    return ESP_OK;
}
static esp_err_t shutdown(void)
{
    if(!s_lock) return ESP_OK;
    xSemaphoreTake(s_lock,portMAX_DELAY); s_standby=true; s_status.standby=true;
    esp_err_t err=ESP_OK,result;
    for(unsigned i=0;i<3;++i) { result=stop_axis(i,true); if(result!=ESP_OK) err=result; }
#if CONFIG_MCU_ENABLE_FAN
    if(s_fan && (result=fan_stop(s_fan))!=ESP_OK) err=result;
#endif
#if CONFIG_MCU_ENABLE_WS2812
    if(s_led && (result=ws2812_stop(s_led))!=ESP_OK) err=result;
#endif
    // Relays retain state; power rail assignments must be decided by the product.
    xSemaphoreGive(s_lock); return err;
}
static void initialize(void)
{
    for(unsigned i=0;i<3;++i) {
        s_status.axis[i].gpio=s_axis_gpio[i];
        s_status.axis[i].configured=s_axis_gpio[i]>=0; s_status.axis[i].error=ESP_ERR_NOT_SUPPORTED;
    }
#if CONFIG_MCU_ENABLE_SERVO
    const int max_angle[]={CONFIG_SAT_AXIS0_MAX,CONFIG_SAT_AXIS1_MAX,CONFIG_SAT_AXIS2_MAX};
    const int min_us[]={CONFIG_SAT_AXIS0_MIN_US,CONFIG_SAT_AXIS1_MIN_US,CONFIG_SAT_AXIS2_MIN_US};
    const int max_us[]={CONFIG_SAT_AXIS0_MAX_US,CONFIG_SAT_AXIS1_MAX_US,CONFIG_SAT_AXIS2_MAX_US};
    bool reversed[3]={false};
#if CONFIG_SAT_AXIS0_REVERSED
    reversed[0]=true;
#endif
#if CONFIG_SAT_AXIS1_REVERSED
    reversed[1]=true;
#endif
#if CONFIG_SAT_AXIS2_REVERSED
    reversed[2]=true;
#endif
    for(unsigned i=0;i<3;++i) if(s_axis_gpio[i]>=0) {
        servo_config_t c=SERVO_CONFIG_DEFAULT(s_axis_gpio[i],SATELLITE_SERVO_TIMER,SATELLITE_SERVO_CHANNEL(i));
        c.min_pulse_us=min_us[i]; c.max_pulse_us=max_us[i]; c.max_angle=max_angle[i];
        c.reversed=reversed[i];
        c.auto_release_ms=CONFIG_SAT_SERVO_AUTO_RELEASE_MS;
        s_status.axis[i].error=servo_create(&c,&s_axes[i].servo);
        s_status.axis[i].ready=s_axes[i].servo!=NULL;
        ESP_LOGI(TAG,"Axis %u: GPIO%d, 50 Hz, 0..%d deg, PWM stopped, init=%s direction=%s auto_release_ms=%lu",
            i,c.gpio,max_angle[i],esp_err_to_name(s_status.axis[i].error),c.reversed?"reversed":"normal",(unsigned long)c.auto_release_ms);
    }
#endif
    for(unsigned i=0;i<4;++i) s_status.voltage_error[i]=s_status.current_error[i]=ESP_ERR_NOT_SUPPORTED;
#if CONFIG_MCU_ENABLE_VOLTAGE_SENSOR
    const uint32_t upper[]={CONFIG_SAT_MON0_V_UPPER,CONFIG_SAT_MON1_V_UPPER,CONFIG_SAT_MON2_V_UPPER,CONFIG_SAT_MON3_V_UPPER};
    const uint32_t lower[]={CONFIG_SAT_MON0_V_LOWER,CONFIG_SAT_MON1_V_LOWER,CONFIG_SAT_MON2_V_LOWER,CONFIG_SAT_MON3_V_LOWER};
    const uint32_t iu[]={CONFIG_SAT_MON0_I_UPPER,CONFIG_SAT_MON1_I_UPPER,CONFIG_SAT_MON2_I_UPPER,CONFIG_SAT_MON3_I_UPPER};
    const uint32_t il[]={CONFIG_SAT_MON0_I_LOWER,CONFIG_SAT_MON1_I_LOWER,CONFIG_SAT_MON2_I_LOWER,CONFIG_SAT_MON3_I_LOWER};
    for(unsigned i=0;i<4;++i) {
        if(s_voltage_gpio[i]>=0) {
            voltage_sensor_config_t c=VOLTAGE_SENSOR_CONFIG_DEFAULT(s_voltage_gpio[i]);
            c.upper_ohms=upper[i]; c.lower_ohms=lower[i];
            s_status.voltage_error[i]=voltage_sensor_create(&c,&s_voltage[i]);
        }
        if(s_current_gpio[i]>=0) {
            voltage_sensor_config_t c=VOLTAGE_SENSOR_CONFIG_DEFAULT(s_current_gpio[i]);
            c.upper_ohms=iu[i]; c.lower_ohms=il[i];
            s_status.current_error[i]=voltage_sensor_create(&c,&s_current[i]);
        }
    }
#endif
    s_status.battery_configured=s_voltage_gpio[2]>=0;
    s_status.power_unknown=s_status.battery_configured;
    s_status.pose_fault=CONFIG_SAT_IMU_SDA>=0;
    s_status.imu_error=s_status.fan_error=s_status.led_error=s_status.relay_error=ESP_ERR_NOT_SUPPORTED;
#if CONFIG_MCU_ENABLE_MPU6050
    if(CONFIG_SAT_IMU_SDA>=0 && CONFIG_SAT_IMU_SCL>=0) {
        mpu6050_config_t c=MPU6050_CONFIG_DEFAULT(CONFIG_SAT_IMU_SDA,CONFIG_SAT_IMU_SCL); c.i2c_port=CONFIG_SAT_IMU_PORT;
        s_status.imu_error=mpu6050_create(&c,&s_imu);
    }
#endif
#if CONFIG_MCU_ENABLE_WS2812
    if(CONFIG_SAT_LED_GPIO>=0) {
        ws2812_config_t c=WS2812_CONFIG_DEFAULT(CONFIG_SAT_LED_GPIO,CONFIG_SAT_LED_COUNT);
        s_status.led_error=ws2812_create(&c,&s_led); s_status.led_ready=s_led!=NULL;
    }
#endif
#if CONFIG_MCU_ENABLE_FAN
    if(CONFIG_SAT_FAN_GPIO>=0) {
        fan_config_t c=FAN_CONFIG_DEFAULT(CONFIG_SAT_FAN_GPIO,CONFIG_SAT_FAN_TACH,2,4);
        s_status.fan_error=fan_create(&c,&s_fan); s_status.fan_ready=s_fan!=NULL;
    }
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
    if(CONFIG_SAT_RELAY_TX>=0) {
        relay_uart_config_t c=RELAY_UART_MODBUS_CONFIG_DEFAULT(1,CONFIG_SAT_RELAY_TX,CONFIG_SAT_RELAY_RX,CONFIG_SAT_RELAY_CHANNELS);
        c.address=CONFIG_SAT_RELAY_ADDR; c.baud_rate=CONFIG_SAT_RELAY_BAUD; c.first_coil=CONFIG_SAT_RELAY_FIRST_COIL; c.rts_gpio=CONFIG_SAT_RELAY_RTS;
        s_status.relay_error=relay_uart_create(&c,&s_relay); s_status.relay_ready=s_relay!=NULL;
    }
#endif
}
static void sample(int64_t now)
{
    for(unsigned i=0;i<4;++i) {
        bool vv=false,iv=false; float voltage=0,current=0;
#if CONFIG_MCU_ENABLE_VOLTAGE_SENSOR
        voltage_sensor_sample_t v;
        if(s_voltage[i]) {
            s_status.voltage_error[i]=voltage_sensor_read(s_voltage[i],&v);
            vv=s_status.voltage_error[i]==ESP_OK; if(vv) voltage=v.voltage_v;
        }
        if(s_current[i]) {
            s_status.current_error[i]=voltage_sensor_read(s_current[i],&v);
            iv=s_status.current_error[i]==ESP_OK;
            if(iv) current=(v.voltage_v*1000-s_zero_mv[i])/s_mv_per_a[i];
        }
#endif
        power_monitor_update(&s_status.power[i],now,vv,voltage,iv,current,3000);
    }
    s_status.power_unknown=s_status.battery_configured && !s_status.power[2].voltage_valid;
    if(s_status.power[2].voltage_valid)
        s_status.low_battery=power_monitor_low(s_status.low_battery,s_status.power[2].voltage_v,
            CONFIG_SAT_BAT_LOW_MV/1000.0f,CONFIG_SAT_BAT_RECOVER_MV/1000.0f);
    s_status.wifi_connected=false;
#if CONFIG_MCU_ENABLE_NETWORK
    wifi_signal_sample_t signal;
    if(wifi_manager_get_signal(&signal)==ESP_OK) {
        s_status.wifi_connected=signal.connected; s_status.rssi_dbm=signal.rssi_dbm;
        memcpy(s_status.bssid,signal.bssid,6);
    }
#endif
    s_status.imu_valid=false;
#if CONFIG_MCU_ENABLE_MPU6050
    if(s_imu) {
        mpu6050_sample_t v; s_status.imu_error=mpu6050_read(s_imu,&v);
        if(s_status.imu_error==ESP_OK) {
            s_status.imu_valid=true;
            memcpy(s_status.accel_m_s2,v.accel_m_s2,sizeof(v.accel_m_s2));
            memcpy(s_status.gyro_dps,v.gyro_dps,sizeof(v.gyro_dps)); s_status.temperature_c=v.temperature_c;
            float norm=sqrtf(v.accel_m_s2[0]*v.accel_m_s2[0]+v.accel_m_s2[1]*v.accel_m_s2[1]+v.accel_m_s2[2]*v.accel_m_s2[2]);
            float gx=CONFIG_SAT_GRAVITY_X_MG,gy=CONFIG_SAT_GRAVITY_Y_MG,gz=CONFIG_SAT_GRAVITY_Z_MG;
            float gn=sqrtf(gx*gx+gy*gy+gz*gz);
            if(norm>0.1f && gn>0) {
                float cosine=(v.accel_m_s2[0]*gx+v.accel_m_s2[1]*gy+v.accel_m_s2[2]*gz)/(norm*gn);
                s_status.tilt_deg=acosf(fmaxf(-1,fminf(1,cosine)))*57.2957795f;
                s_tilt_count=s_status.tilt_deg>CONFIG_SAT_TILT_LIMIT_DEG?s_tilt_count+1:0;
                s_status.pose_fault=s_tilt_count>=3 || norm>CONFIG_SAT_SHOCK_MG*0.00980665f;
            } else s_status.pose_fault=true;
        } else s_status.pose_fault=true;
    } else if(CONFIG_SAT_IMU_SDA>=0) s_status.pose_fault=true;
#endif
    s_status.sample_us=now;
}
#if CONFIG_MCU_ENABLE_SERVO
static esp_err_t start_search(unsigned i)
{
    servo_status_t v; servo_get_status(s_axes[i].servo,&v);
    if((!v.output_enabled && !v.auto_released) || !v.angle_known || v.moving || blocked()) return ESP_ERR_INVALID_STATE;
    if(i==0 && !s_status.wifi_connected) return ESP_ERR_INVALID_STATE;
    if(i && (!s_status.power[i-1].power_valid || s_status.power[i-1].power_w<CONFIG_SAT_SOLAR_MIN_MW/1000.0f)) return ESP_ERR_INVALID_STATE;
    float span=v.max_angle-v.min_angle;
    angle_search_config_t c={.min_angle=v.min_angle,.max_angle=v.max_angle,
        .coarse_step=fminf(10,span),.fine_step=fminf(2,span),.samples=3,
        .settle_ms=CONFIG_SAT_SEARCH_SETTLE_MS,.timeout_ms=15000,
        .min_score=i?CONFIG_SAT_SOLAR_MIN_MW/1000.0f:-100,.improvement=i?0.001f:2};
    memcpy(s_axes[i].bssid,s_status.bssid,6);
    return angle_search_start(&s_axes[i].search,&c);
}
#endif
static void control(int64_t now)
{
    for(unsigned i=0;i<3;++i) {
#if CONFIG_MCU_ENABLE_SERVO
        if(!s_axes[i].servo) continue;
        if(blocked()) stop_axis(i,false);
        servo_status_t v; servo_get_status(s_axes[i].servo,&v);
        if((s_status.axis[i].mode==SAT_MODE_AUTO || s_status.axis[i].mode==SAT_MODE_SCAN)) {
            angle_search_t *search=&s_axes[i].search;
            if(v.last_error!=ESP_OK) { stop_axis(i,false); s_status.axis[i].error=v.last_error; }
            else {
                if(!angle_search_active(search) && now>=s_axes[i].next_search_us) {
                    esp_err_t err=start_search(i);
                    if(err!=ESP_OK) { s_status.axis[i].mode=SAT_MODE_MANUAL; s_status.axis[i].error=err; }
                }
                bool valid=i?s_status.power[i-1].power_valid:s_status.wifi_connected;
                float score=i?s_status.power[i-1].power_w:s_status.rssi_dbm;
                if(i==0 && (!valid || memcmp(s_axes[i].bssid,s_status.bssid,6))) {
                    stop_axis(i,false); s_status.axis[i].error=ESP_ERR_INVALID_STATE;
                } else if(i && valid && score<CONFIG_SAT_SOLAR_MIN_MW/1000.0f) {
                    stop_axis(i,false); s_status.axis[i].error=ESP_ERR_NOT_FOUND;
                } else if((s_status.axis[i].mode==SAT_MODE_AUTO || s_status.axis[i].mode==SAT_MODE_SCAN)) {
                    float target;
                    if(angle_search_tick(search,now,v.moving,valid,score,s_status.sample_us,&target)) {
                        esp_err_t err=servo_move_to(s_axes[i].servo,target,1000,SERVO_CURVE_SMOOTH);
                        if(err!=ESP_OK) { stop_axis(i,false); s_status.axis[i].error=err; }
                    }
                    if(search->phase==ANGLE_SEARCH_FAILED) {
                        esp_err_t err=search->error; servo_cancel_motion(s_axes[i].servo);
                        s_status.axis[i].mode=SAT_MODE_MANUAL; s_status.axis[i].error=err;
                    } else if(search->phase==ANGLE_SEARCH_DONE && s_axes[i].next_search_us<=now) {
                        s_axes[i].next_search_us=now+(int64_t)CONFIG_SAT_AUTO_INTERVAL_S*1000000;
                        if(s_status.axis[i].mode==SAT_MODE_SCAN) s_status.axis[i].mode=SAT_MODE_MANUAL;
                    }
                }
            }
        }
        servo_get_status(s_axes[i].servo,&v);
        satellite_axis_status_t *a=&s_status.axis[i];
        a->output_enabled=v.output_enabled; a->angle_known=v.angle_known; a->moving=v.moving;
        a->auto_released=v.auto_released;
        a->angle=v.commanded_angle; a->target=v.target_angle; a->home_known=v.home_known; a->home=v.home_angle; a->max_angle=v.max_angle;
        a->search_phase=s_axes[i].search.phase; a->best_valid=s_axes[i].search.have_best;
        a->best_angle=s_axes[i].search.best_angle; a->best_score=s_axes[i].search.best_score;
        if(v.last_error!=ESP_OK) a->error=v.last_error;
#endif
    }
#if CONFIG_MCU_ENABLE_FAN
    if(s_fan) {
        fan_status_t f; s_status.fan_error=fan_get_status(s_fan,&f);
        if(s_status.fan_error==ESP_OK) { s_status.fan_duty=f.duty_percent; s_status.fan_rpm_valid=f.rpm_valid; s_status.fan_rpm=f.rpm; }
    }
#endif
#if CONFIG_MCU_ENABLE_WS2812
    if(s_led) {
        int state=s_standby?0:(s_status.pose_fault||s_status.power_unknown)?1:s_status.low_battery?2:s_status.wifi_connected?3:4;
        if(state!=s_led_state) {
            ws2812_effect_config_t e=WS2812_EFFECT_DEFAULT(WS2812_EFFECT_BREATH);
            e.color=state==1?(ws2812_color_t){255,0,0}:state==2?(ws2812_color_t){255,100,0}:state==3?(ws2812_color_t){0,255,0}:(ws2812_color_t){0,80,255};
            e.brightness=state==2?s_brightness/4:s_brightness;
            s_status.led_error=state==0?ws2812_stop(s_led):ws2812_set_effect(s_led,&e);
            if(s_status.led_error==ESP_OK) s_led_state=state;
        }
    }
#endif
    (void)now;
}
static void task(void *arg)
{
    (void)arg; int64_t next=0;
    for(;;) {
        xSemaphoreTake(s_lock,portMAX_DELAY);
        int64_t now=esp_timer_get_time();
        if(now>=next) { sample(now); next=now+1000000; }
        control(now);
        xSemaphoreGive(s_lock); vTaskDelay(pdMS_TO_TICKS(50));
    }
}
esp_err_t satellite_start(void)
{
    if(s_lock) return ESP_ERR_INVALID_STATE;
    if(CONFIG_SAT_BAT_RECOVER_MV<=CONFIG_SAT_BAT_LOW_MV) return ESP_ERR_INVALID_ARG;
    s_lock=xSemaphoreCreateMutex(); if(!s_lock) return ESP_ERR_NO_MEM;
    initialize();
    esp_err_t err=runtime_register_shutdown_hook(shutdown);
    if(err!=ESP_OK) return err;
    if(xTaskCreate(task,"satellite",6144,NULL,4,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG,"Ready: servo PWM waits for an explicit angle command; use 'sat help'");
    return ESP_OK;
}
esp_err_t satellite_get_status(satellite_status_t *out)
{
    if(!s_lock || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock,portMAX_DELAY); *out=s_status; xSemaphoreGive(s_lock); return ESP_OK;
}
esp_err_t satellite_axis_command(unsigned i,const char *action,float value,uint32_t duration)
{
    if(!s_lock || i>=3 || !action || !isfinite(value) || duration>600000) return ESP_ERR_INVALID_ARG;
    const char *allowed[]={"angle","set","move","by","home","record","limit","hold","stop","scan","auto","demo","manual"};
    bool known=false; for(unsigned k=0;k<sizeof(allowed)/sizeof(*allowed);k++) if(!strcmp(action,allowed[k])) known=true;
    if(!known) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock,portMAX_DELAY); esp_err_t err=ESP_ERR_NOT_SUPPORTED;
#if CONFIG_MCU_ENABLE_SERVO
    servo_handle_t servo=s_axes[i].servo;
    if(servo) {
        bool stop=!strcmp(action,"stop"),hold=!strcmp(action,"hold")||!strcmp(action,"manual");
        if(stop || hold) err=stop_axis(i,stop);
        else if(blocked()) err=ESP_ERR_INVALID_STATE;
        else if(!strcmp(action,"auto") || !strcmp(action,"scan")) {
            err=start_search(i);
            if(err==ESP_OK) { s_status.axis[i].mode=!strcmp(action,"scan")?SAT_MODE_SCAN:SAT_MODE_AUTO; s_axes[i].next_search_us=0; }
        } else {
            servo_status_t before; servo_get_status(servo,&before);
            bool configure=!strcmp(action,"record") || !strcmp(action,"limit");
            // Capturing home or changing limits must never silently stop an active trajectory.
            err=configure && before.moving?ESP_ERR_INVALID_STATE:ESP_OK;
            if(err==ESP_OK) {
                esp_err_t stopped=stop_axis(i,false);
                if(stopped!=ESP_OK && strcmp(action,"set") && strcmp(action,"angle")) err=stopped;
            }
            if(err==ESP_OK) {
                if(!strcmp(action,"angle")) {
                    // One application-level angle operation: bootstrap unknown/stopped
                    // outputs directly, otherwise resume from the last issued command.
                    if(before.angle_known && (before.output_enabled || before.auto_released) && before.last_error==ESP_OK)
                        err=servo_move_to(servo,value,SATELLITE_DEFAULT_MOVE_MS,SERVO_CURVE_SMOOTH);
                    else err=servo_set_angle(servo,value);
                }
                else if(!strcmp(action,"set")) err=servo_set_angle(servo,value);
                else if(!strcmp(action,"move")) err=servo_move_to(servo,value,duration,SERVO_CURVE_SMOOTH);
                else if(!strcmp(action,"by")) err=servo_move_by(servo,value,duration,SERVO_CURVE_SMOOTH);
                else if(!strcmp(action,"record")) err=servo_record_home(servo);
                else if(!strcmp(action,"home")) err=servo_return_home(servo,duration,SERVO_CURVE_SMOOTH);
                else if(!strcmp(action,"limit")) err=servo_set_limits(servo,0,value);
                else if(!strcmp(action,"demo")) {
                    servo_status_t v; servo_get_status(servo,&v);
                    err=servo_sweep(servo,0,v.max_angle,duration?duration:3000,1000);
                    if(err==ESP_OK) s_status.axis[i].mode=SAT_MODE_DEMO;
                } else err=ESP_ERR_INVALID_ARG;
            }
        }
        s_status.axis[i].error=err;
    }
#endif
    xSemaphoreGive(s_lock); return err;
}
esp_err_t satellite_get_pwm_status(unsigned axis,satellite_pwm_status_t *out)
{
    if(!s_lock || !out || axis>=SATELLITE_AXES) return ESP_ERR_INVALID_ARG;
    esp_err_t err=ESP_ERR_NOT_SUPPORTED;
#if CONFIG_MCU_ENABLE_SERVO
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_axes[axis].servo) {
        servo_pwm_status_t pwm;
        err=servo_get_pwm_status(s_axes[axis].servo,&pwm);
        if(err==ESP_OK) *out=(satellite_pwm_status_t){.backend=pwm.backend,.gpio=s_axis_gpio[axis],
            .hardware_readback=pwm.hardware_readback,.output_enabled=pwm.output_enabled,
            .frequency_hz=pwm.frequency_hz,.compare_ticks=pwm.compare_ticks,.period_ticks=pwm.period_ticks};
    }
    xSemaphoreGive(s_lock);
#endif
    return err;
}
esp_err_t satellite_command(const char *action,unsigned channel,float value)
{
    if(!s_lock || !action || !isfinite(value)) return ESP_ERR_INVALID_ARG;
    if(!strcmp(action,"standby")) return shutdown();
    xSemaphoreTake(s_lock,portMAX_DELAY); esp_err_t err=ESP_ERR_NOT_SUPPORTED;
    if(!strcmp(action,"resume")) { s_standby=false; s_status.standby=false; err=ESP_OK; }
#if CONFIG_MCU_ENABLE_FAN
    else if(!strcmp(action,"fan") && s_fan) {
        err=value>=0 && value<=100 && floorf(value)==value ? fan_set_duty(s_fan,value):ESP_ERR_INVALID_ARG;
    }
#endif
#if CONFIG_MCU_ENABLE_WS2812
    else if(!strcmp(action,"led") && s_led) {
        err=value>=0 && value<=255 && floorf(value)==value?ESP_OK:ESP_ERR_INVALID_ARG;
        if(err==ESP_OK) { s_brightness=value; s_led_state=-1; }
    }
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
    else if(!strcmp(action,"relay") && s_relay) {
        err=value==0 || value==1?relay_uart_set(s_relay,channel,value!=0):ESP_ERR_INVALID_ARG;
    }
#endif
    (void)channel;
    xSemaphoreGive(s_lock); return err;
}

esp_err_t satellite_get_scan(unsigned axis,angle_search_point_t *points,size_t capacity,size_t *count)
{
    if(!s_lock || axis>=3 || !points || !count) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    *count=s_axes[axis].search.point_count;
    esp_err_t err=capacity<*count?ESP_ERR_INVALID_SIZE:ESP_OK;
    if(err==ESP_OK) memcpy(points,s_axes[axis].search.points,*count*sizeof(*points));
    xSemaphoreGive(s_lock); return err;
}
