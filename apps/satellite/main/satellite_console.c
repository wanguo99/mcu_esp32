#include "satellite.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_MCU_ENABLE_SERVO
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#endif
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static bool number(const char *s,float *out) {
    if(!s || !*s) return false;
    char *end; errno=0; float v=strtof(s,&end);
    if(errno || *end || !isfinite(v)) return false;
    *out=v; return true;
}
static void status(void) {
    satellite_status_t s;
    if(satellite_get_status(&s)!=ESP_OK) return;
    printf("policy standby=%d low_battery=%d power_unknown=%d pose_fault=%d\n",s.standby,s.low_battery,s.power_unknown,s.pose_fault);
    if(s.wifi_connected) printf("wifi rssi=%d dBm mean=%.1f min=%d max=%d samples=%u ssid=%s channel=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x sample_us=%lld\n",
        s.rssi_dbm,s.rssi_mean_dbm,s.rssi_min_dbm,s.rssi_max_dbm,s.wifi_samples,s.wifi_ssid,s.wifi_channel,
        s.bssid[0],s.bssid[1],s.bssid[2],s.bssid[3],s.bssid[4],s.bssid[5],(long long)s.wifi_sample_us);
    else printf("wifi signal=unavailable error=%s (configure station SSID and connect)\n",esp_err_to_name(s.wifi_error));
    printf("solar common_voltage_channel=0 automatic=%d boot_wait=%d active_axis=%u pending_right=%d\n",s.solar_automatic,s.solar_boot_pending,s.solar_active_axis,s.solar_pending_right);
    const char *names[]={"antenna","left","right"};
    for(unsigned i=0;i<3;i++) {
        satellite_axis_status_t *a=&s.axis[i];
        printf("%s gpio=%d configured=%d ready=%d known=%d PWM=%d auto_released=%d moving=%d angle=%.2f target=%.2f home_known=%d home=%.2f max=%.2f mode=%d search=%d best_valid=%d best_angle=%.2f best_score=%.3f unit=%s error=%s\n",
            names[i],a->gpio,a->configured,a->ready,a->angle_known,a->output_enabled,a->auto_released,a->moving,a->angle,a->target,a->home_known,a->home,a->max_angle,a->mode,a->search_phase,a->best_valid,a->best_angle,a->best_score,i?"V":"dBm",esp_err_to_name(a->error));
    }
    const char *mon[]={"solar_bus","auxiliary","battery","external"};
    for(unsigned i=0;i<4;i++) {
        power_monitor_t *p=&s.power[i];
        printf("%s ",mon[i]);
        if(p->voltage_valid) printf("voltage=%.3fV ",p->voltage_v); else printf("voltage=unavailable(%s) ",esp_err_to_name(s.voltage_error[i]));
        if(p->power_valid) printf("current=%.4fA power=%.4fW ",p->current_a,p->power_w); else printf("power=unavailable(%s) ",esp_err_to_name(s.current_error[i]));
        printf("session_energy=%.6fWh\n",p->energy_wh);
    }
    if(s.imu_valid) printf("imu tilt=%.1fdeg chip_temperature=%.1fC accel=%.3f,%.3f,%.3f gyro=%.3f,%.3f,%.3f\n",s.tilt_deg,s.temperature_c,s.accel_m_s2[0],s.accel_m_s2[1],s.accel_m_s2[2],s.gyro_dps[0],s.gyro_dps[1],s.gyro_dps[2]);
    else printf("imu unavailable: %s\n",esp_err_to_name(s.imu_error));
    printf("fan ready=%d duty=%u rpm_valid=%d rpm=%.1f error=%s; led ready=%d error=%s; relay ready=%d error=%s\n",
        s.fan_ready,s.fan_duty,s.fan_rpm_valid,s.fan_rpm,esp_err_to_name(s.fan_error),s.led_ready,esp_err_to_name(s.led_error),s.relay_ready,esp_err_to_name(s.relay_error));
}
static int pwm_diagnostics(void) {
#if CONFIG_MCU_ENABLE_SERVO
    satellite_status_t s;
    if(satellite_get_status(&s)!=ESP_OK) return 1;
    for(unsigned i=0;i<SATELLITE_AXES;i++) {
        if(!s.axis[i].ready) continue;
        satellite_pwm_status_t pwm;
        if(satellite_get_pwm_status(i,&pwm)!=ESP_OK) return 1;
        printf("PWM %s axis=%u GPIO%d backend=%s freq_hz=%lu compare_ticks=%lu/%lu pulse_us=%.2f output_enabled=%d\n",
            pwm.hardware_readback?"hardware":"command",i,pwm.gpio,pwm.backend,
            (unsigned long)pwm.frequency_hz,(unsigned long)pwm.compare_ticks,(unsigned long)pwm.period_ticks,
            pwm.frequency_hz?(double)pwm.compare_ticks*1000000/(pwm.period_ticks*(double)pwm.frequency_hz):0,
            pwm.output_enabled);
        gpio_dump_io_configuration(stdout,UINT64_C(1)<<s.axis[i].gpio);
        gpio_io_config_t io;
        if(gpio_get_io_config(s.axis[i].gpio,&io)==ESP_OK && io.ie) {
            unsigned high=0,low=0,rises=0;
            int previous=gpio_get_level(s.axis[i].gpio);
            int64_t start=esp_timer_get_time(),last_rise=0,period_sum=0;
            while(esp_timer_get_time()-start<60000) {
                int level=gpio_get_level(s.axis[i].gpio);
                if(level) high++; else low++;
                if(level && !previous) {
                    int64_t now=esp_timer_get_time();
                    if(last_rise) period_sum+=now-last_rise;
                    last_rise=now; rises++;
                }
                previous=level;
                esp_rom_delay_us(200);
            }
            printf("GPIO%d pad samples (60ms): high=%u low=%u rising_edges=%u approximate_period_us=%.0f\n",
                s.axis[i].gpio,high,low,rises,rises>1?(double)period_sum/(rises-1):0);
        } else puts("Pad sampling unavailable: GPIO input buffer is disabled");
    }
    return 0;
#else
    puts("Servo module is not enabled"); return 1;
#endif
}
// Observe PWM values during a bounded move. This measures commands,
// not the mechanical shaft position; it deliberately has no HTTP dependency.
static int timing(unsigned axis,bool smooth,float angle,uint32_t duration)
{
#if CONFIG_MCU_ENABLE_SERVO
    satellite_status_t status;
    if(axis>=SATELLITE_AXES || satellite_get_status(&status)!=ESP_OK || !status.axis[axis].ready) return 1;
    struct sample { uint32_t elapsed_us,duty; };
    const unsigned capacity=256;
    struct sample *samples=calloc(capacity,sizeof(*samples));
    if(!samples) { puts("TIMING: not enough memory"); return 1; }
    int64_t start=esp_timer_get_time();
    esp_err_t err=satellite_axis_command(axis,smooth?"move":"set",angle,duration);
    int64_t call_us=esp_timer_get_time()-start;
    if(err!=ESP_OK) { free(samples); printf("TIMING: %s\n",esp_err_to_name(err)); return 1; }
    uint32_t last=UINT32_MAX;
    unsigned count=0;
    int64_t end=start+(int64_t)(smooth?duration+100:100)*1000;
    for(;;) {
        int64_t now=esp_timer_get_time();
        satellite_pwm_status_t pwm;
        if(satellite_get_pwm_status(axis,&pwm)!=ESP_OK) { free(samples); return 1; }
        uint32_t duty=pwm.compare_ticks;
        if(duty!=last || now>=end) {
            if(count<capacity) samples[count++]=(struct sample){(uint32_t)(now-start),duty};
            last=duty;
        }
        if(now>=end) break;
        TickType_t wait=pdMS_TO_TICKS(10); vTaskDelay(wait?wait:1);
    }
    // UART printing is deferred until sampling ends so it cannot distort the trace.
    printf("TIMING axis=%u action=%s target=%.2f duration_ms=%u call_us=%lld result=%s\n",
        axis,smooth?"move":"set",(double)angle,(unsigned)duration,(long long)call_us,esp_err_to_name(err));
    satellite_pwm_status_t pwm;
    if(satellite_get_pwm_status(axis,&pwm)!=ESP_OK) { free(samples); return 1; }
    for(unsigned i=0;i<count;i++)
        printf("TIMING t_us=%lu GPIO%d compare_ticks=%lu/%lu backend=%s source=%s freq_hz=%lu pulse_us=%.2f\n",
            (unsigned long)samples[i].elapsed_us,pwm.gpio,(unsigned long)samples[i].duty,
            (unsigned long)pwm.period_ticks,pwm.backend,pwm.hardware_readback?"hardware":"command",
            (unsigned long)pwm.frequency_hz,pwm.frequency_hz?(double)samples[i].duty*1000000/
            (pwm.period_ticks*(double)pwm.frequency_hz):0);
    free(samples);
    puts("TIMING done: PWM values only, no shaft position feedback");
    return 0;
#else
    (void)axis; (void)smooth; (void)angle; (void)duration;
    puts("Servo module is not enabled"); return 1;
#endif
}
#if CONFIG_MCU_ENABLE_SERVO
static bool capture_done(rmt_channel_handle_t channel,const rmt_rx_done_event_data_t *event,void *context)
{
    (void)channel;
    BaseType_t wake=pdFALSE;
    xQueueSendFromISR((QueueHandle_t)context,event,&wake);
    return wake==pdTRUE;
}
#endif

// RMT observes the physical pad input without changing its output routing.
static int capture(unsigned axis,bool smooth,float angle,uint32_t duration,bool paired)
{
#if CONFIG_MCU_ENABLE_SERVO
    satellite_status_t status;
    if(axis>=SATELLITE_AXES || satellite_get_status(&status)!=ESP_OK || !status.axis[axis].ready) return 1;
    if(paired && (!status.axis[axis].angle_known || (!status.axis[axis].output_enabled && !status.axis[axis].auto_released))) {
        puts("CAPTURE pair needs an enabled starting angle; use set first"); return 1;
    }
    const unsigned capacity=128;
    float final_angle=paired?status.axis[axis].angle:angle;
    rmt_symbol_word_t *symbols=calloc(capacity,sizeof(*symbols));
    QueueHandle_t done=xQueueCreate(1,sizeof(rmt_rx_done_event_data_t));
    if(!symbols || !done) { free(symbols); if(done)vQueueDelete(done); return 1; }
    rmt_channel_handle_t rx=NULL;
    bool enabled=false;
    rmt_rx_channel_config_t config={.gpio_num=status.axis[axis].gpio,.clk_src=RMT_CLK_SRC_DEFAULT,
        .resolution_hz=1000000,.mem_block_symbols=capacity};
    esp_err_t err=rmt_new_rx_channel(&config,&rx);
    rmt_rx_event_callbacks_t callbacks={.on_recv_done=capture_done};
    if(err==ESP_OK) err=rmt_rx_register_event_callbacks(rx,&callbacks,done);
    if(err==ESP_OK) { err=rmt_enable(rx); enabled=err==ESP_OK; }
    rmt_receive_config_t receive={.signal_range_min_ns=1000,.signal_range_max_ns=30000000};
    int64_t start=esp_timer_get_time(),offset=0;
    bool commanded=false,paused=false;
    if(err==ESP_OK) err=rmt_receive(rx,symbols,capacity*sizeof(*symbols),&receive);
    if(err==ESP_OK) {
        offset=esp_timer_get_time()-start;
        err=satellite_axis_command(axis,smooth?"move":"set",angle,duration);
        commanded=err==ESP_OK;
    }
    int64_t second_offset=0,pause_offset=0;
    if(err==ESP_OK && paired) {
        vTaskDelay(pdMS_TO_TICKS(duration+250));
        second_offset=esp_timer_get_time()-start;
        err=satellite_axis_command(axis,"move",final_angle,duration);
    }
    if(err==ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS((smooth?duration:0)+100));
        // Continuous PWM has no idle delimiter. End RX after the observation
        // window by stopping PWM, never between the two paired movements.
        pause_offset=esp_timer_get_time()-start;
        err=satellite_axis_command(axis,"stop",0,duration);
        paused=err==ESP_OK;
    }
    rmt_rx_done_event_data_t event={0};
    if(err==ESP_OK && xQueueReceive(done,&event,pdMS_TO_TICKS(200))!=pdTRUE) err=ESP_ERR_TIMEOUT;
    if(enabled) {
        esp_err_t stopped=rmt_disable(rx);
        if(err==ESP_OK) err=stopped;
    }
    bool channel_released=true;
    if(rx) {
        esp_err_t deleted=rmt_del_channel(rx);
        channel_released=deleted==ESP_OK;
        if(err==ESP_OK) err=deleted;
    }
    if(paused && commanded) {
        esp_err_t resumed=satellite_axis_command(axis,"set",final_angle,duration);
        if(err==ESP_OK) err=resumed;
    }
    if(event.num_symbols>capacity) err=ESP_ERR_INVALID_SIZE;
    printf("CAPTURE axis=%u GPIO%d action=%s target=%.2f duration_ms=%u command_offset_us=%lld result=%s symbols=%u paired=%d return_target=%.2f second_offset_us=%lld pause_offset_us=%lld\n",
        axis,status.axis[axis].gpio,smooth?"move":"set",(double)angle,(unsigned)duration,
        (long long)offset,esp_err_to_name(err),(unsigned)event.num_symbols,paired,(double)final_angle,
        (long long)second_offset,(long long)pause_offset);
    if(err==ESP_OK) {
        uint32_t elapsed=0,last_start=0;
        bool have_start=false;
        for(size_t i=0;i<event.num_symbols;i++) {
            unsigned times[]={symbols[i].duration0,symbols[i].duration1};
            unsigned levels[]={symbols[i].level0,symbols[i].level1};
            for(unsigned j=0;j<2;j++) {
                if(!times[j]) continue;
                // The first level can be a partial high pulse; do not treat it as a full pulse.
                if(levels[j] && elapsed) {
                    printf("CAPTURE pulse_start_us=%lu high_us=%u period_us=%lu\n",(unsigned long)elapsed,
                        times[j],(unsigned long)(have_start?elapsed-last_start:0));
                    last_start=elapsed; have_start=true;
                }
                elapsed+=times[j];
            }
        }
    }
    // Retain callback storage if the driver could not detach its interrupt.
    if(channel_released) { free(symbols); vQueueDelete(done); }
    else puts("CAPTURE cleanup incomplete: callback storage retained until restart");
    puts("CAPTURE done: RMT measured pad pulse widths; no shaft position feedback");
    return err==ESP_OK?0:1;
#else
    (void)axis;(void)smooth;(void)angle;(void)duration;(void)paired;
    puts("Servo module is not enabled"); return 1;
#endif
}
static int command(int argc,char **argv) {
    if(argc==1 || (argc==2 && !strcmp(argv[1],"help"))) {
        puts("sat status | pwm | standby | resume | trace antenna|left|right\n"
             "sat axis antenna|left|right angle|set|move|by|limit VALUE [DURATION_MS]\n"
             "sat axis antenna|left|right record|home|hold|stop|manual|scan|auto|demo\n"
             "sat timing antenna|left|right set|move ANGLE [DURATION_MS=500, max 2000]\n"
             "sat capture antenna|left|right set|move ANGLE [DURATION_MS=500, max 1000]\n"
             "sat capturepair antenna|left|right TARGET [DURATION_MS=500, max 1000]\n"
             "capture ends sampling with a brief PWM pause, then restores the target; paired moves stay continuous.\n"
             "sat fan DUTY_PERCENT | led BRIGHTNESS | relay CHANNEL 0|1\n"
             "set establishes initial command; auto requires measured RSSI or solar voltage; status angles are commands, not feedback.");
        return 0;
    }
    if(argc==2 && !strcmp(argv[1],"status")) { status(); return 0; }
    if(argc==2 && !strcmp(argv[1],"pwm")) return pwm_diagnostics();
    if((argc==4 || argc==5) && !strcmp(argv[1],"capturepair")) {
        unsigned axis=!strcmp(argv[2],"antenna")?0:!strcmp(argv[2],"left")?1:!strcmp(argv[2],"right")?2:3;
        float angle,duration=SATELLITE_DEFAULT_MOVE_MS;
        if(!number(argv[3],&angle) || (argc==5 && !number(argv[4],&duration)) ||
            duration<1 || duration>1000 || floorf(duration)!=duration) return 1;
        return capture(axis,true,angle,(uint32_t)duration,true);
    }
    if((argc==5 || argc==6) && (!strcmp(argv[1],"timing") || !strcmp(argv[1],"capture"))) {
        unsigned axis=!strcmp(argv[2],"antenna")?0:!strcmp(argv[2],"left")?1:!strcmp(argv[2],"right")?2:3;
        float angle,duration=SATELLITE_DEFAULT_MOVE_MS;
        if((strcmp(argv[3],"set") && strcmp(argv[3],"move")) || !number(argv[4],&angle) ||
            (argc==6 && !number(argv[5],&duration)) || duration<1 ||
            duration>(!strcmp(argv[1],"capture")?1000:2000) || floorf(duration)!=duration)
            return 1;
        if(!strcmp(argv[1],"capture")) return capture(axis,!strcmp(argv[3],"move"),angle,(uint32_t)duration,false);
        return timing(axis,!strcmp(argv[3],"move"),angle,(uint32_t)duration);
    }
    if(argc==3 && !strcmp(argv[1],"trace")) {
        unsigned axis=!strcmp(argv[2],"antenna")?0:!strcmp(argv[2],"left")?1:!strcmp(argv[2],"right")?2:3;
        angle_search_point_t *points=calloc(ANGLE_SEARCH_MAX_POINTS,sizeof(*points));
        if(!points) return 1;
        size_t count=0; esp_err_t result=satellite_get_scan(axis,points,ANGLE_SEARCH_MAX_POINTS,&count);
        if(result==ESP_OK) { puts("angle,score"); for(size_t i=0;i<count;i++) printf("%.3f,%.6f\n",points[i].angle,points[i].score); }
        else printf("%s\n",esp_err_to_name(result));
        free(points); return result==ESP_OK?0:1;
    }
    esp_err_t err=ESP_ERR_INVALID_ARG; float value=0,duration=SATELLITE_DEFAULT_MOVE_MS;
    if(argc>=4 && argc<=6 && !strcmp(argv[1],"axis")) {
        unsigned axis=!strcmp(argv[2],"antenna")?0:!strcmp(argv[2],"left")?1:!strcmp(argv[2],"right")?2:3;
        bool needs_value=!strcmp(argv[3],"angle")||!strcmp(argv[3],"set")||!strcmp(argv[3],"move")||!strcmp(argv[3],"by")||!strcmp(argv[3],"limit");
        if((needs_value && argc>=5 && number(argv[4],&value)) || (!needs_value && argc==4)) {
            if(argc!=6 || number(argv[5],&duration)) {
                if(duration>=1 && duration<=600000 && floorf(duration)==duration)
                    err=satellite_axis_command(axis,argv[3],value,(uint32_t)duration);
            }
        }
    } else if(argc==2 && (!strcmp(argv[1],"standby")||!strcmp(argv[1],"resume"))) err=satellite_command(argv[1],0,0);
    else if(argc==3 && (!strcmp(argv[1],"fan")||!strcmp(argv[1],"led")) && number(argv[2],&value)) err=satellite_command(argv[1],0,value);
    else if(argc==4 && !strcmp(argv[1],"relay")) {
        float channel;
        if(number(argv[2],&channel) && channel>=0 && channel<32 && floorf(channel)==channel && number(argv[3],&value)) err=satellite_command("relay",channel,value);
    }
    printf("%s\n",esp_err_to_name(err)); return err==ESP_OK?0:1;
}
esp_err_t satellite_console_start(void) {
    esp_console_repl_config_t cfg=ESP_CONSOLE_REPL_CONFIG_DEFAULT(); cfg.prompt="satellite>"; cfg.max_cmdline_length=160;
    esp_console_repl_t *repl=NULL; esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t dev=ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err=esp_console_new_repl_usb_serial_jtag(&dev,&cfg,&repl);
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t dev=ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    err=esp_console_new_repl_usb_cdc(&dev,&cfg,&repl);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t dev=ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err=esp_console_new_repl_uart(&dev,&cfg,&repl);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if(err!=ESP_OK) return err;
    const esp_console_cmd_t cmd={.command="sat",.help="Satellite control; sat help for commands",.func=command};
    err=esp_console_cmd_register(&cmd);
    if(err!=ESP_OK) { repl->del(repl); return err; }
    return esp_console_start_repl(repl);
}
