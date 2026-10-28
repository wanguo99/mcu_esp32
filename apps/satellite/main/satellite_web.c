// Optional HTTP adapter. Satellite core has no HTTP or JSON dependencies.
#include "sdkconfig.h"
#if CONFIG_MCU_ENABLE_WEB
#include "satellite.h"
#include "web_server.h"
#include "esp_log.h"
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
static const char *TAG="satellite_web";
extern const uint8_t page_start[] asm("_binary_satellite_html_start");
extern const uint8_t page_end[] asm("_binary_satellite_html_end");
extern const uint8_t css_start[] asm("_binary_satellite_css_start");
extern const uint8_t css_end[] asm("_binary_satellite_css_end");
extern const uint8_t js_start[] asm("_binary_satellite_js_start");
extern const uint8_t js_end[] asm("_binary_satellite_js_end");
static esp_err_t page_handler(httpd_req_t *r) { return web_send_asset(r,page_start,page_end,"text/html; charset=utf-8"); }
static esp_err_t css_handler(httpd_req_t *r) { return web_send_asset(r,css_start,css_end,"text/css; charset=utf-8"); }
static esp_err_t js_handler(httpd_req_t *r) { return web_send_asset(r,js_start,js_end,"application/javascript; charset=utf-8"); }
static void number(cJSON *o,const char *key,bool valid,double value) {
    if(valid) cJSON_AddNumberToObject(o,key,value); else cJSON_AddNullToObject(o,key);
}
static esp_err_t status_handler(httpd_req_t *req) {
    satellite_status_t s; satellite_get_status(&s);
    cJSON *root=cJSON_CreateObject(); if(!root) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(root,"standby",s.standby);
    cJSON_AddBoolToObject(root,"low_battery",s.low_battery); cJSON_AddBoolToObject(root,"power_unknown",s.power_unknown);
    cJSON_AddBoolToObject(root,"pose_fault",s.pose_fault); cJSON_AddNumberToObject(root,"sample_us",s.sample_us);
    number(root,"rssi_dbm",s.wifi_connected,s.rssi_dbm);
    cJSON *axes=cJSON_AddArrayToObject(root,"axes"),*power=cJSON_AddArrayToObject(root,"power");
    if(!axes || !power) { cJSON_Delete(root); return httpd_resp_send_500(req); }
    for(unsigned i=0;i<3;i++) {
        satellite_axis_status_t *a=&s.axis[i]; cJSON *o=cJSON_CreateObject();
        if(!o) { cJSON_Delete(root); return httpd_resp_send_500(req); }
        cJSON_AddItemToArray(axes,o); cJSON_AddBoolToObject(o,"configured",a->configured); cJSON_AddBoolToObject(o,"ready",a->ready);
        number(o,"gpio",a->configured,a->gpio);
        number(o,"angle",a->angle_known,a->angle); number(o,"home",a->home_known,a->home);
        cJSON_AddNumberToObject(o,"target",a->target); cJSON_AddNumberToObject(o,"max_angle",a->max_angle);
        cJSON_AddBoolToObject(o,"moving",a->moving); cJSON_AddBoolToObject(o,"output_enabled",a->output_enabled);
        cJSON_AddBoolToObject(o,"auto_released",a->auto_released);
        cJSON_AddNumberToObject(o,"mode",a->mode); cJSON_AddNumberToObject(o,"search_phase",a->search_phase);
        number(o,"best_angle",a->best_valid,a->best_angle); number(o,"best_score",a->best_valid,a->best_score);
        cJSON_AddStringToObject(o,"error",esp_err_to_name(a->error));
    }
    for(unsigned i=0;i<4;i++) {
        power_monitor_t *p=&s.power[i]; cJSON *o=cJSON_CreateObject();
        if(!o) { cJSON_Delete(root); return httpd_resp_send_500(req); }
        cJSON_AddItemToArray(power,o); number(o,"voltage_v",p->voltage_valid,p->voltage_v);
        number(o,"current_a",p->power_valid,p->current_a); number(o,"power_w",p->power_valid,p->power_w);
        cJSON_AddNumberToObject(o,"session_energy_wh",p->energy_wh);
        cJSON_AddStringToObject(o,"voltage_error",esp_err_to_name(s.voltage_error[i]));
        cJSON_AddStringToObject(o,"current_error",esp_err_to_name(s.current_error[i]));
    }
    number(root,"tilt_deg",s.imu_valid,s.tilt_deg); number(root,"chip_temperature_c",s.imu_valid,s.temperature_c);
    cJSON_AddStringToObject(root,"imu_error",esp_err_to_name(s.imu_error));
    cJSON_AddBoolToObject(root,"fan_ready",s.fan_ready);
    cJSON_AddBoolToObject(root,"led_ready",s.led_ready);
    cJSON_AddBoolToObject(root,"relay_ready",s.relay_ready);
    cJSON_AddNumberToObject(root,"relay_channels",CONFIG_SAT_RELAY_CHANNELS);
    number(root,"fan_duty",s.fan_ready,s.fan_duty); number(root,"fan_rpm",s.fan_rpm_valid,s.fan_rpm);
    cJSON_AddStringToObject(root,"fan_error",esp_err_to_name(s.fan_error));
    cJSON_AddStringToObject(root,"led_error",esp_err_to_name(s.led_error));
    cJSON_AddStringToObject(root,"relay_error",esp_err_to_name(s.relay_error));
    return web_send_json(req,root);
}
static bool param(const char *q,const char *key,float fallback,float *out) {
    char text[40],*end; esp_err_t err=httpd_query_key_value(q,key,text,sizeof(text));
    if(err==ESP_ERR_NOT_FOUND) { *out=fallback; return true; }
    if(err!=ESP_OK || !text[0]) return false;
    errno=0; *out=strtof(text,&end); return !errno && !*end && isfinite(*out);
}
static esp_err_t command_handler(httpd_req_t *req) {
    if(web_server_is_restarting()) return web_send_json_error(req,"503 Service Unavailable","restarting","Restart pending");
    char q[200],action[24]; float axis,value,duration,channel;
    if(httpd_req_get_url_query_str(req,q,sizeof(q))!=ESP_OK || httpd_query_key_value(q,"action",action,sizeof(action))!=ESP_OK ||
       !param(q,"axis",-1,&axis) || !param(q,"value",0,&value) || !param(q,"duration_ms",SATELLITE_DEFAULT_MOVE_MS,&duration) || !param(q,"channel",0,&channel) ||
       axis<-1 || axis>2 || floorf(axis)!=axis || duration<1 || duration>600000 || floorf(duration)!=duration || channel<0 || channel>31 || floorf(channel)!=channel)
        return web_send_json_error(req,"400 Bad Request","invalid_parameters","Check axis, action, value and duration_ms");
    if((!strcmp(action,"angle") || !strcmp(action,"set") || !strcmp(action,"move") || !strcmp(action,"by") || !strcmp(action,"limit") ||
        !strcmp(action,"fan") || !strcmp(action,"led") || !strcmp(action,"relay")) && httpd_query_key_value(q,"value",(char[40]){0},40)!=ESP_OK)
        return web_send_json_error(req,"400 Bad Request","missing_value","This command requires value");
    esp_err_t err=axis<0?satellite_command(action,channel,value):satellite_axis_command(axis,action,value,duration);
    const int gpios[]={CONFIG_SAT_AXIS0_GPIO,CONFIG_SAT_AXIS1_GPIO,CONFIG_SAT_AXIS2_GPIO};
    const char *roles[]={"antenna","left","right"};
    ESP_LOGI(TAG,"Command axis=%d action=%s value=%.3f duration_ms=%u result=%s gpio=%d role=%s",
        (int)axis,action,(double)value,(unsigned)duration,esp_err_to_name(err),
        axis>=0?gpios[(unsigned)axis]:-1,axis>=0?roles[(unsigned)axis]:"global");
    if(err!=ESP_OK) return web_send_json_error(req,err==ESP_ERR_INVALID_ARG?"400 Bad Request":"409 Conflict",esp_err_to_name(err),"Command rejected; check hardware readiness, limits, mode and power/pose status");
    cJSON *reply=cJSON_CreateObject(); if(!reply) return httpd_resp_send_500(req);
    cJSON_AddBoolToObject(reply,"ok",true); return web_send_json(req,reply);
}
static esp_err_t scan_handler(httpd_req_t *req) {
    char query[40]; float axis;
    if(httpd_req_get_url_query_str(req,query,sizeof(query))!=ESP_OK || !param(query,"axis",-1,&axis) || axis<0 || axis>2 || floorf(axis)!=axis)
        return web_send_json_error(req,"400 Bad Request","invalid_axis","axis must be 0..2");
    angle_search_point_t *points=calloc(ANGLE_SEARCH_MAX_POINTS,sizeof(*points));
    cJSON *root=cJSON_CreateObject();
    if(!points || !root) { free(points); cJSON_Delete(root); return httpd_resp_send_500(req); }
    size_t count; satellite_get_scan(axis,points,ANGLE_SEARCH_MAX_POINTS,&count);
    cJSON *array=cJSON_AddArrayToObject(root,"points");
    if(!array) { free(points); cJSON_Delete(root); return httpd_resp_send_500(req); }
    for(size_t i=0;i<count;i++) {
        double values[]={points[i].angle,points[i].score}; cJSON *pair=cJSON_CreateDoubleArray(values,2);
        if(!pair) { free(points); cJSON_Delete(root); return httpd_resp_send_500(req); }
        cJSON_AddItemToArray(array,pair);
    }
    free(points); return web_send_json(req,root);
}
esp_err_t satellite_web_start(void) {
    const httpd_uri_t routes[]={
        {.uri="/satellite/",.method=HTTP_GET,.handler=page_handler},
        {.uri="/satellite/styles.css",.method=HTTP_GET,.handler=css_handler},
        {.uri="/satellite/app.js",.method=HTTP_GET,.handler=js_handler},
        {.uri="/api/satellite/status",.method=HTTP_GET,.handler=status_handler},
        {.uri="/api/satellite/command",.method=HTTP_POST,.handler=command_handler},
        {.uri="/api/satellite/scan",.method=HTTP_GET,.handler=scan_handler},
    };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) { esp_err_t err=httpd_register_uri_handler(web_server_handle(),&routes[i]); if(err!=ESP_OK) return err; }
    return web_server_register_page("卫星控制台","/satellite/");
}
#endif
