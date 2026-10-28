#include "console.h"
#include "console_input.h"
#include "runtime.h"
#include "sdkconfig.h"
#include "console_config.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>
#if CONFIG_MCU_ENABLE_SERVO
#include "servo.h"
static servo_handle_t s_servo;
#endif
#if CONFIG_MCU_ENABLE_FAN
#include "fan.h"
static fan_handle_t s_fan;
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
#include "relay_uart.h"
static relay_uart_handle_t s_relay;
static unsigned s_relay_channels;
#endif
#if CONFIG_MCU_ENABLE_MPU6050
#include "mpu6050.h"
static mpu6050_handle_t s_imu;
#endif
#if CONFIG_MCU_ENABLE_WS2812
#include "ws2812.h"
static ws2812_handle_t s_led;
#endif
#if CONFIG_MCU_ENABLE_WEB
#include "web_server.h"
#endif

enum { MOD_SERVO, MOD_FAN, MOD_RELAY, MOD_IMU, MOD_LED, MOD_COUNT };
static esp_err_t s_initialization[MOD_COUNT];
static const char *TAG = "component_demo";

static esp_err_t shutdown_peripherals(void)
{
    esp_err_t result = ESP_OK, current;
    (void)current;
#if CONFIG_MCU_ENABLE_SERVO
    if (s_servo && (current=servo_stop(s_servo)) != ESP_OK) result=current;
#endif
#if CONFIG_MCU_ENABLE_FAN
    if (s_fan && (current=fan_stop(s_fan)) != ESP_OK) result=current;
#endif
#if CONFIG_MCU_ENABLE_WS2812
    if (s_led && (current=ws2812_stop(s_led)) != ESP_OK) result=current;
#endif
#if CONFIG_MCU_ENABLE_MPU6050
    if (s_imu && (current=mpu6050_set_sleep(s_imu,true)) != ESP_OK) result=current;
#endif
    // Relay contact state is intentionally retained: switching on reboot is a product policy.
    return result;
}

static void initialize_peripherals(void)
{
    for (unsigned i=0;i<MOD_COUNT;++i) s_initialization[i]=ESP_ERR_NOT_SUPPORTED;
#if CONFIG_DEMO_SERVO
    servo_config_t config = SERVO_CONFIG_DEFAULT(CONFIG_DEMO_SERVO_GPIO,1,1);
    config.min_pulse_us=CONFIG_DEMO_SERVO_MIN_US; config.max_pulse_us=CONFIG_DEMO_SERVO_MAX_US;
    config.max_angle=CONFIG_DEMO_SERVO_MAX_ANGLE;
    s_initialization[MOD_SERVO]=servo_create(&config,&s_servo);
#endif
#if CONFIG_DEMO_FAN
    fan_config_t fan = FAN_CONFIG_DEFAULT(CONFIG_DEMO_FAN_PWM,CONFIG_DEMO_FAN_TACH,2,2);
    fan.pulses_per_revolution=CONFIG_DEMO_FAN_PPR; fan.pwm_frequency_hz=CONFIG_DEMO_FAN_HZ;
    fan.pwm_inverted=CONFIG_DEMO_FAN_INVERTED;
    s_initialization[MOD_FAN]=fan_create(&fan,&s_fan);
#endif
#if CONFIG_DEMO_RELAY
#if CONFIG_DEMO_RELAY_FIXED
    relay_uart_channel_frames_t frames = {0};
    esp_err_t parsed=console_parse_hex(CONFIG_DEMO_RELAY_ON_HEX,frames.on.data,RELAY_UART_MAX_FRAME,&frames.on.length);
    if (parsed==ESP_OK) parsed=console_parse_hex(CONFIG_DEMO_RELAY_OFF_HEX,frames.off.data,RELAY_UART_MAX_FRAME,&frames.off.length);
    if (parsed==ESP_OK) parsed=console_parse_hex(CONFIG_DEMO_RELAY_ACK_ON_HEX,frames.ack_on.data,RELAY_UART_MAX_FRAME,&frames.ack_on.length);
    if (parsed==ESP_OK) parsed=console_parse_hex(CONFIG_DEMO_RELAY_ACK_OFF_HEX,frames.ack_off.data,RELAY_UART_MAX_FRAME,&frames.ack_off.length);
    relay_uart_config_t relay = RELAY_UART_FIXED_CONFIG_DEFAULT(CONFIG_DEMO_RELAY_UART,CONFIG_DEMO_RELAY_TX,CONFIG_DEMO_RELAY_RX,1,&frames);
#else
    esp_err_t parsed=ESP_OK;
    relay_uart_config_t relay = RELAY_UART_MODBUS_CONFIG_DEFAULT(CONFIG_DEMO_RELAY_UART,CONFIG_DEMO_RELAY_TX,CONFIG_DEMO_RELAY_RX,CONFIG_DEMO_RELAY_CHANNELS);
    relay.address=CONFIG_DEMO_RELAY_ADDRESS; relay.first_coil=CONFIG_DEMO_RELAY_FIRST_COIL;
#endif
    relay.rts_gpio=CONFIG_DEMO_RELAY_RTS; relay.baud_rate=CONFIG_DEMO_RELAY_BAUD;
    relay.parity=CONFIG_DEMO_RELAY_PARITY==1 ? UART_PARITY_ODD : (CONFIG_DEMO_RELAY_PARITY==2 ? UART_PARITY_EVEN : UART_PARITY_DISABLE);
    relay.stop_bits=CONFIG_DEMO_RELAY_TWO_STOP ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
    s_initialization[MOD_RELAY]=parsed==ESP_OK ? relay_uart_create(&relay,&s_relay) : parsed;
    s_relay_channels=relay.channel_count;
#endif
#if CONFIG_DEMO_IMU
    mpu6050_config_t imu = MPU6050_CONFIG_DEFAULT(CONFIG_DEMO_IMU_SDA,CONFIG_DEMO_IMU_SCL);
    imu.i2c_port=CONFIG_DEMO_IMU_PORT; imu.address=CONFIG_DEMO_IMU_AD0_HIGH ? 0x69 : 0x68;
    imu.internal_pullups=CONFIG_DEMO_IMU_INTERNAL_PULLUPS;
    s_initialization[MOD_IMU]=mpu6050_create(&imu,&s_imu);
#endif
#if CONFIG_DEMO_LED
    ws2812_config_t led = WS2812_CONFIG_DEFAULT(CONFIG_DEMO_LED_GPIO,CONFIG_DEMO_LED_COUNT);
    led.color_order=CONFIG_DEMO_LED_RGB ? WS2812_ORDER_RGB : WS2812_ORDER_GRB;
    s_initialization[MOD_LED]=ws2812_create(&led,&s_led);
#endif
    for (unsigned i=0;i<MOD_COUNT;++i)
        if (s_initialization[i]!=ESP_OK && s_initialization[i]!=ESP_ERR_NOT_SUPPORTED)
            ESP_LOGE(TAG,"Peripheral %u initialization failed: %s",i,esp_err_to_name(s_initialization[i]));
}

#if CONFIG_MCU_ENABLE_WEB
extern const uint8_t console_start[] asm("_binary_console_html_start");
extern const uint8_t console_end[] asm("_binary_console_html_end");
static esp_err_t page_handler(httpd_req_t *req)
{ return web_send_asset(req,console_start,console_end,"text/html; charset=utf-8"); }

static __attribute__((unused)) cJSON *module_status(cJSON *root,const char *name,unsigned id,bool configured,bool ready)
{
    cJSON *item=cJSON_AddObjectToObject(root,name);
    if (!item) return NULL;
    cJSON_AddBoolToObject(item,"configured",configured); cJSON_AddBoolToObject(item,"ready",ready);
    cJSON_AddStringToObject(item,"error",configured && s_initialization[id]!=ESP_OK ? esp_err_to_name(s_initialization[id]) : "");
    return item;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    if (web_server_is_restarting()) return web_send_json_error(req,"503 Service Unavailable","restarting","Restart pending");
    cJSON *root=cJSON_CreateObject(), *item=NULL;
    if (!root) goto no_memory;
    (void)item;
#if CONFIG_MCU_ENABLE_SERVO
    item=module_status(root,"servo",MOD_SERVO,CONFIG_DEMO_SERVO,s_servo!=NULL);
    if (!item) goto no_memory;
    if (s_servo) {
        servo_status_t value; servo_get_status(s_servo,&value);
        cJSON_AddBoolToObject(item,"output_enabled",value.output_enabled); cJSON_AddBoolToObject(item,"moving",value.moving);
        if (value.angle_known) cJSON_AddNumberToObject(item,"commanded_angle",value.commanded_angle);
        else cJSON_AddNullToObject(item,"commanded_angle");
        cJSON_AddNumberToObject(item,"target_angle",value.target_angle); cJSON_AddStringToObject(item,"last_error",esp_err_to_name(value.last_error));
#if CONFIG_DEMO_SERVO
        cJSON_AddNumberToObject(item,"max_angle",value.max_angle);
        cJSON_AddNumberToObject(item,"min_angle",value.min_angle);
        cJSON_AddNumberToObject(item,"calibrated_max_angle",CONFIG_DEMO_SERVO_MAX_ANGLE);
        if (value.home_known) cJSON_AddNumberToObject(item,"home_angle",value.home_angle);
        else cJSON_AddNullToObject(item,"home_angle");
#endif
    }
#endif
#if CONFIG_MCU_ENABLE_FAN
    item=module_status(root,"fan",MOD_FAN,CONFIG_DEMO_FAN,s_fan!=NULL);
    if (!item) goto no_memory;
    if (s_fan) {
        fan_status_t value; fan_get_status(s_fan,&value);
        cJSON_AddNumberToObject(item,"duty_percent",value.duty_percent); cJSON_AddBoolToObject(item,"rpm_valid",value.rpm_valid);
        cJSON_AddBoolToObject(item,"tach_available",value.tach_available); cJSON_AddNumberToObject(item,"sample_age_ms",value.sample_age_ms);
        if (value.rpm_valid) cJSON_AddNumberToObject(item,"rpm",value.rpm); else cJSON_AddNullToObject(item,"rpm");
    }
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART
    item=module_status(root,"relay_uart",MOD_RELAY,CONFIG_DEMO_RELAY,s_relay!=NULL);
    if (!item) goto no_memory;
    if (s_relay) {
        cJSON_AddNumberToObject(item,"channel_count",s_relay_channels);
#if CONFIG_DEMO_RELAY
        cJSON_AddBoolToObject(item,"readback_supported",!CONFIG_DEMO_RELAY_FIXED);
#endif
    }
#endif
#if CONFIG_MCU_ENABLE_MPU6050
    item=module_status(root,"mpu6050",MOD_IMU,CONFIG_DEMO_IMU,s_imu!=NULL);
    if (!item) goto no_memory;
    if (s_imu) {
        mpu6050_sample_t value; esp_err_t result=mpu6050_read(s_imu,&value);
        cJSON_AddStringToObject(item,"read_error",result==ESP_OK ? "" : esp_err_to_name(result));
        if (result==ESP_OK) {
            double accel[]={value.accel_m_s2[0],value.accel_m_s2[1],value.accel_m_s2[2]};
            double gyro[]={value.gyro_dps[0],value.gyro_dps[1],value.gyro_dps[2]};
            cJSON_AddItemToObject(item,"accel_m_s2",cJSON_CreateDoubleArray(accel,3));
            cJSON_AddItemToObject(item,"gyro_dps",cJSON_CreateDoubleArray(gyro,3));
            cJSON_AddNumberToObject(item,"temperature_c",value.temperature_c); cJSON_AddNumberToObject(item,"timestamp_us",value.timestamp_us);
        }
    }
#endif
#if CONFIG_MCU_ENABLE_WS2812
    item=module_status(root,"ws2812",MOD_LED,CONFIG_DEMO_LED,s_led!=NULL);
    if (!item) goto no_memory;
    if (s_led) {
        ws2812_status_t value; ws2812_get_status(s_led,&value);
        cJSON_AddNumberToObject(item,"effect",value.effect.effect); cJSON_AddStringToObject(item,"last_error",esp_err_to_name(value.last_error));
        cJSON_AddNumberToObject(item,"frames_sent",value.frames_sent);
#if CONFIG_DEMO_LED
        cJSON_AddNumberToObject(item,"led_count",CONFIG_DEMO_LED_COUNT);
#endif
    }
#endif
    return web_send_json(req,root);
no_memory:
    cJSON_Delete(root); return httpd_resp_send_500(req);
}

typedef struct { char data[384]; } query_t;
static __attribute__((unused)) esp_err_t query_number(const query_t *q,const char *name,double min,double max,double fallback,double *out)
{
    char text[48]; esp_err_t result=httpd_query_key_value(q->data,name,text,sizeof(text));
    if (result==ESP_ERR_NOT_FOUND) { *out=fallback; return ESP_OK; }
    return result==ESP_OK ? console_parse_number(text,min,max,out) : ESP_ERR_INVALID_ARG;
}
static __attribute__((unused)) esp_err_t query_integer(const query_t *q,const char *name,uint32_t min,uint32_t max,uint32_t fallback,uint32_t *out)
{
    double number; esp_err_t result=query_number(q,name,min,max,fallback,&number);
    if (result!=ESP_OK || floor(number)!=number) return ESP_ERR_INVALID_ARG;
    *out=(uint32_t)number; return ESP_OK;
}

static esp_err_t command_handler(httpd_req_t *req)
{
    if (web_server_is_restarting()) return web_send_json_error(req,"503 Service Unavailable","restarting","Restart pending");
    query_t q; char module[24],action[24];
    if (httpd_req_get_url_query_str(req,q.data,sizeof(q.data))!=ESP_OK ||
        httpd_query_key_value(q.data,"module",module,sizeof(module))!=ESP_OK ||
        httpd_query_key_value(q.data,"action",action,sizeof(action))!=ESP_OK)
        return web_send_json_error(req,"400 Bad Request","invalid_query","module and action are required");
    esp_err_t result=ESP_ERR_NOT_SUPPORTED;
    cJSON *reply=cJSON_CreateObject();
    if (!reply) return httpd_resp_send_500(req);
#if CONFIG_MCU_ENABLE_SERVO && CONFIG_DEMO_SERVO
    if (!strcmp(module,"servo")) {
        result=ESP_ERR_INVALID_STATE;
        if (s_servo) {
            if (!strcmp(action,"stop")) result=servo_stop(s_servo);
            else if (!strcmp(action,"hold")) result=servo_cancel_motion(s_servo);
            else if (!strcmp(action,"record_home")) result=servo_record_home(s_servo);
            else if (!strcmp(action,"relative") || !strcmp(action,"home")) {
                uint32_t duration; double delta=0;
                result=query_integer(&q,"duration_ms",1,600000,1000,&duration);
                if (result==ESP_OK && !strcmp(action,"relative"))
                    result=query_number(&q,"delta",-CONFIG_DEMO_SERVO_MAX_ANGLE,CONFIG_DEMO_SERVO_MAX_ANGLE,0,&delta);
                if (result==ESP_OK) result=!strcmp(action,"home") ? servo_return_home(s_servo,duration,SERVO_CURVE_SMOOTH) :
                    servo_move_by(s_servo,(float)delta,duration,SERVO_CURVE_SMOOTH);
            }
            else if (!strcmp(action,"limits")) {
                double maximum;
                result=query_number(&q,"max",0,CONFIG_DEMO_SERVO_MAX_ANGLE,CONFIG_DEMO_SERVO_MAX_ANGLE,&maximum);
                if (result==ESP_OK) result=servo_set_limits(s_servo,0,(float)maximum);
            }
            else if (!strcmp(action,"set") || !strcmp(action,"move") || !strcmp(action,"sweep")) {
                double angle=0,low=0,high=0; uint32_t duration=1000;
                bool sweep=!strcmp(action,"sweep"), move=!strcmp(action,"move");
                result=ESP_OK;
                if (!sweep) result=query_number(&q,"angle",0,CONFIG_DEMO_SERVO_MAX_ANGLE,CONFIG_DEMO_SERVO_MAX_ANGLE/2.0,&angle);
                if (sweep) result=query_number(&q,"min",0,CONFIG_DEMO_SERVO_MAX_ANGLE,0,&low);
                if (sweep && result==ESP_OK) result=query_number(&q,"max",0,CONFIG_DEMO_SERVO_MAX_ANGLE,CONFIG_DEMO_SERVO_MAX_ANGLE,&high);
                if ((move || sweep) && result==ESP_OK) result=query_integer(&q,"duration_ms",1,600000,1000,&duration);
                if (result==ESP_OK) {
                    if (sweep) result=servo_sweep(s_servo,(float)low,(float)high,duration,200);
                    else if (move) result=servo_move_to(s_servo,(float)angle,duration,SERVO_CURVE_SMOOTH);
                    else result=servo_set_angle(s_servo,(float)angle);
                }
            } else result=ESP_ERR_INVALID_ARG;
        }
    }
#endif
#if CONFIG_MCU_ENABLE_FAN && CONFIG_DEMO_FAN
    if (!strcmp(module,"fan")) {
        result=ESP_ERR_INVALID_STATE;
        if (s_fan) {
            uint32_t duty;
            if (!strcmp(action,"stop")) result=fan_stop(s_fan);
            else if (!strcmp(action,"set")) {
                result=query_integer(&q,"duty",0,100,0,&duty);
                if (result==ESP_OK) result=fan_set_duty(s_fan,duty);
            } else result=ESP_ERR_INVALID_ARG;
        }
    }
#endif
#if CONFIG_MCU_ENABLE_RELAY_UART && CONFIG_DEMO_RELAY
    if (!strcmp(module,"relay_uart")) {
        result=ESP_ERR_INVALID_STATE;
        if (s_relay) {
            uint32_t channel,on;
            result=query_integer(&q,"channel",0,s_relay_channels-1,0,&channel);
            if (result==ESP_OK) result=query_integer(&q,"on",0,1,0,&on);
            if (result==ESP_OK) {
                if (!strcmp(action,"read")) {
                    bool state; result=relay_uart_read(s_relay,channel,&state);
                    if (result==ESP_OK) cJSON_AddBoolToObject(reply,"coil_on",state);
                } else if (!strcmp(action,"set")) {
                    result=relay_uart_set(s_relay,channel,on!=0);
                    relay_uart_command_status_t value; relay_uart_get_command_status(s_relay,channel,&value);
                    cJSON_AddBoolToObject(reply,"command_sent",value.command_sent);
                    cJSON_AddBoolToObject(reply,"acknowledged",value.acknowledged);
                } else result=ESP_ERR_INVALID_ARG;
            }
        }
    }
#endif
#if CONFIG_MCU_ENABLE_MPU6050 && CONFIG_DEMO_IMU
    if (!strcmp(module,"mpu6050")) {
        result=ESP_ERR_INVALID_STATE;
        if (s_imu) result=(!strcmp(action,"sleep") || !strcmp(action,"wake")) ?
            mpu6050_set_sleep(s_imu,!strcmp(action,"sleep")) : ESP_ERR_INVALID_ARG;
    }
#endif
#if CONFIG_MCU_ENABLE_WS2812 && CONFIG_DEMO_LED
    if (!strcmp(module,"ws2812")) {
        result=ESP_ERR_INVALID_STATE;
        if (s_led) {
            if (!strcmp(action,"stop")) result=ws2812_stop(s_led);
            else if (!strcmp(action,"set")) {
                uint32_t kind,r,g,b,brightness,period,tail,reverse;
                result=query_integer(&q,"effect",0,4,1,&kind);
                if (result==ESP_OK) result=query_integer(&q,"r",0,255,255,&r);
                if (result==ESP_OK) result=query_integer(&q,"g",0,255,255,&g);
                if (result==ESP_OK) result=query_integer(&q,"b",0,255,255,&b);
                if (result==ESP_OK) result=query_integer(&q,"brightness",0,255,32,&brightness);
                if (result==ESP_OK) result=query_integer(&q,"period_ms",100,60000,2000,&period);
                if (result==ESP_OK) result=query_integer(&q,"tail",1,CONFIG_DEMO_LED_COUNT,1,&tail);
                if (result==ESP_OK) result=query_integer(&q,"reverse",0,1,0,&reverse);
                if (result==ESP_OK) {
                    ws2812_effect_config_t effect={.effect=kind,.color={r,g,b},.brightness=brightness,
                        .period_ms=period,.tail_length=tail,.reverse=reverse!=0};
                    result=ws2812_set_effect(s_led,&effect);
                }
            } else result=ESP_ERR_INVALID_ARG;
        }
    }
#endif
    if (result!=ESP_OK) {
        cJSON_Delete(reply);
        return web_send_json_error(req,result==ESP_ERR_INVALID_ARG ? "400 Bad Request" : "409 Conflict",
            esp_err_to_name(result),"Command failed: check configuration, device readiness and parameters; establish a servo start angle before moving");
    }
    cJSON_AddBoolToObject(reply,"ok",true); return web_send_json(req,reply);
}
#endif

esp_err_t component_console_start(void)
{
    initialize_peripherals();
    esp_err_t result=runtime_register_shutdown_hook(shutdown_peripherals);
    if (result!=ESP_OK) return result;
#if CONFIG_MCU_ENABLE_WEB
    const httpd_uri_t routes[]={
        {.uri="/components/",.method=HTTP_GET,.handler=page_handler},
        {.uri="/api/components/status",.method=HTTP_GET,.handler=status_handler},
        {.uri="/api/components/command",.method=HTTP_POST,.handler=command_handler},
    };
    for (unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);++i) {
        result=httpd_register_uri_handler(web_server_handle(),&routes[i]);
        if (result!=ESP_OK) return result;
    }
    result=web_server_register_page("组件调试","/components/");
#endif
    ESP_LOGI(TAG,"Component console ready; configure physical wiring in menuconfig -> Component demo wiring");
    return result;
}
