// Standalone bench firmware: shared square wave, wiring check or SG90 angle sweep.
#include "sdkconfig.h"
#include "board.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>

static const char *TAG="gpio_pwm_test";
#define TEST_TIMER LEDC_TIMER_1
#define TEST_CHANNEL LEDC_CHANNEL_1
#define TEST_MODE LEDC_LOW_SPEED_MODE
#if !CONFIG_GPIO_PWM_TEST_LEVEL_TOGGLE && !CONFIG_GPIO_PWM_TEST_SERVO_SWEEP
#if CONFIG_IDF_TARGET_ESP32
static const int pins[]={4,13,14,18,19,21,22,23,25,26,27,32,33,
#if CONFIG_GPIO_PWM_TEST_INCLUDE_GPIO16_17
    16,17,
#endif
};
#elif CONFIG_IDF_TARGET_ESP32S3
// XIAO D0..D7. SD, camera and microphone connections remain untouched.
static const int pins[]={1,2,3,4,5,6,43,44};
#else
#error "GPIO PWM test pin mapping is not defined for this chip"
#endif
#define PIN_COUNT (sizeof(pins)/sizeof(pins[0]))
#define TEST_DUTY 512U
#endif

void app_main(void)
{
#if CONFIG_GPIO_PWM_TEST_LEVEL_TOGGLE
    const gpio_num_t pin=GPIO_NUM_13;
    gpio_config_t io={.pin_bit_mask=UINT64_C(1)<<pin,.mode=GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_set_level(pin,0));
    ESP_ERROR_CHECK(gpio_config(&io));
    ESP_LOGI(TAG,"GPIO13 wiring test: LOW 1s, HIGH 1s; PWM/Wi-Fi not started");
    for(;;) {
        ESP_ERROR_CHECK(gpio_set_level(pin,0));
        ESP_LOGI(TAG,"GPIO13 LOW, pad=%d (hold 1000ms)",gpio_get_level(pin));
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_ERROR_CHECK(gpio_set_level(pin,1));
        ESP_LOGI(TAG,"GPIO13 HIGH, pad=%d (hold 1000ms)",gpio_get_level(pin));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
#elif CONFIG_GPIO_PWM_TEST_SERVO_SWEEP
    const struct { gpio_num_t pin; ledc_channel_t channel; const char *name; } outputs[]={
        {CONFIG_GPIO_PWM_TEST_SERVO_GPIO,TEST_CHANNEL,"servo 1"},
#if CONFIG_GPIO_PWM_TEST_SECOND_SERVO
        {CONFIG_GPIO_PWM_TEST_SECOND_SERVO_GPIO,LEDC_CHANNEL_2,"servo 2"},
#endif
    };
    enum { SERVO_COUNT=sizeof(outputs)/sizeof(outputs[0]) };
    for(unsigned i=0;i<SERVO_COUNT;i++) {
        ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(outputs[i].pin)?ESP_OK:ESP_ERR_INVALID_ARG);
        for(unsigned j=0;j<i;j++)
            ESP_ERROR_CHECK(outputs[i].pin!=outputs[j].pin?ESP_OK:ESP_ERR_INVALID_ARG);
    }
#if CONFIG_GPIO_PWM_TEST_SERVO_TWO_ANGLES
    const unsigned angles[]={45,135};
    const char *sequence="45/135";
#else
    const unsigned angles[]={0,45,90,135,180};
    const char *sequence="0/45/90/135/180";
#endif
    const unsigned hold_ms=CONFIG_GPIO_PWM_TEST_SERVO_HOLD_MS;
    ledc_timer_config_t timer={.speed_mode=TEST_MODE,.timer_num=TEST_TIMER,
        .duty_resolution=LEDC_TIMER_14_BIT,.freq_hz=50,.clk_cfg=LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    for(unsigned i=0;i<SERVO_COUNT;i++) {
        gpio_num_t pin=outputs[i].pin;
        // Let LEDC own the output path; keep input enabled for pad sampling.
        // Configuring GPIO output first would reserve it twice in ESP-IDF 6.1.
        ESP_ERROR_CHECK(gpio_reset_pin(pin));
        ESP_ERROR_CHECK(gpio_set_level(pin,0));
        ESP_ERROR_CHECK(gpio_set_pull_mode(pin,GPIO_FLOATING));
        ESP_ERROR_CHECK(gpio_input_enable(pin));
        ledc_channel_config_t channel={.gpio_num=pin,.speed_mode=TEST_MODE,.channel=outputs[i].channel,
            .timer_sel=TEST_TIMER,.duty=0,.hpoint=0};
        ESP_ERROR_CHECK(ledc_channel_config(&channel));
        ESP_LOGI(TAG,"%s signal=GPIO%d LEDC channel=%d",outputs[i].name,pin,outputs[i].channel);
    }
    ESP_LOGI(TAG,"Servo sweep: %u channel(s), 50 Hz; %s degrees, hold=%u ms each; Wi-Fi not started",
        (unsigned)SERVO_COUNT,sequence,hold_ms);
    // Let initial channel updates finish before starting the sweep clock.
    vTaskDelay(pdMS_TO_TICKS(40));
    TickType_t next=xTaskGetTickCount();
    unsigned cycles=0;
    for(unsigned index=0;;index=(index+1)%(sizeof(angles)/sizeof(angles[0]))) {
        unsigned angle=angles[index];
        unsigned pulse_us=500U+angle*2000U/180U;
        unsigned duty=(pulse_us*16384U+10000U)/20000U;
        for(unsigned i=0;i<SERVO_COUNT;i++) {
            ESP_ERROR_CHECK(ledc_set_duty(TEST_MODE,outputs[i].channel,duty));
            ESP_ERROR_CHECK(ledc_update_duty(TEST_MODE,outputs[i].channel));
        }
        // Readback changes at the next PWM period, not immediately on update.
        vTaskDelay(pdMS_TO_TICKS(40));
        for(unsigned i=0;i<SERVO_COUNT;i++)
            ESP_LOGI(TAG,"GPIO%d angle=%u deg pulse=%u us freq=%lu Hz duty=%lu/16384 (hold %ums) servo=%s",
                outputs[i].pin,angle,pulse_us,(unsigned long)ledc_get_freq(TEST_MODE,TEST_TIMER),
                (unsigned long)ledc_get_duty(TEST_MODE,outputs[i].channel),hold_ms,outputs[i].name);
        // Sample three PWM periods after the update has reached hardware.
        unsigned high[SERVO_COUNT]={0},low[SERVO_COUNT]={0},rising[SERVO_COUNT]={0};
        int previous[SERVO_COUNT];
        for(unsigned i=0;i<SERVO_COUNT;i++) previous[i]=gpio_get_level(outputs[i].pin);
        int64_t start=esp_timer_get_time();
        while(esp_timer_get_time()-start<60000) {
            for(unsigned i=0;i<SERVO_COUNT;i++) {
                int level=gpio_get_level(outputs[i].pin);
                high[i]+=level!=0;
                low[i]+=level==0;
                rising[i]+=level && !previous[i];
                previous[i]=level;
            }
            esp_rom_delay_us(137);
        }
        for(unsigned i=0;i<SERVO_COUNT;i++)
            ESP_LOGI(TAG,"GPIO%d pad samples: high=%u low=%u rising=%u in 60ms",outputs[i].pin,high[i],low[i],rising[i]);
        vTaskDelayUntil(&next,pdMS_TO_TICKS(hold_ms));
        if(index+1==sizeof(angles)/sizeof(angles[0]) && CONFIG_GPIO_PWM_TEST_SERVO_CYCLES &&
           ++cycles>=CONFIG_GPIO_PWM_TEST_SERVO_CYCLES) {
            for(unsigned i=0;i<SERVO_COUNT;i++) ESP_ERROR_CHECK(ledc_stop(TEST_MODE,outputs[i].channel,0));
            ESP_LOGI(TAG,"Completed %u cycles; all servo PWM stopped, signals LOW",cycles);
            for(;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
#else
    ESP_LOGI(TAG,"Board=%s; GPIO square-wave test (not a servo angle command)",board_get_config()->name);
    ESP_LOGI(TAG,"Disconnect external signal wires before testing; use board GND for the scope");
    uint64_t mask=0;
    for(unsigned i=0;i<PIN_COUNT;i++) {
        ESP_ERROR_CHECK(GPIO_IS_VALID_OUTPUT_GPIO(pins[i])?ESP_OK:ESP_ERR_INVALID_ARG);
        mask|=UINT64_C(1)<<pins[i];
    }
    gpio_config_t io={.pin_bit_mask=mask,.mode=GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&io));
    ledc_timer_config_t timer={.speed_mode=TEST_MODE,.timer_num=TEST_TIMER,
        .duty_resolution=LEDC_TIMER_10_BIT,.freq_hz=CONFIG_GPIO_PWM_TEST_FREQUENCY,.clk_cfg=LEDC_AUTO_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel={.gpio_num=pins[0],.speed_mode=TEST_MODE,.channel=TEST_CHANNEL,
        .timer_sel=TEST_TIMER,.duty=TEST_DUTY,.hpoint=0};
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    for(unsigned i=1;i<PIN_COUNT;i++) ESP_ERROR_CHECK(ledc_set_pin(pins[i],TEST_MODE,TEST_CHANNEL));
    // Each GPIO uses the same signal, with identical frequency, duty and phase.
    ESP_ERROR_CHECK(ledc_set_duty(TEST_MODE,TEST_CHANNEL,TEST_DUTY));
    ESP_ERROR_CHECK(ledc_update_duty(TEST_MODE,TEST_CHANNEL));
    for(unsigned i=0;i<PIN_COUNT;i++) ESP_LOGI(TAG,"GPIO%d: %d Hz, 50%% duty",pins[i],CONFIG_GPIO_PWM_TEST_FREQUENCY);
    gpio_dump_io_configuration(stdout,mask);
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Allow the first hardware period to complete before readback.
        ESP_LOGI(TAG,"Hardware readback: freq=%lu Hz duty=%lu/1024",
            (unsigned long)ledc_get_freq(TEST_MODE,TEST_TIMER),(unsigned long)ledc_get_duty(TEST_MODE,TEST_CHANNEL));
        unsigned high[PIN_COUNT]={0},samples=0;
        int64_t start=esp_timer_get_time();
        // Non-synchronous sampling avoids repeatedly sampling the same PWM phase.
        while(esp_timer_get_time()-start<30000) {
            for(unsigned i=0;i<PIN_COUNT;i++) high[i]+=gpio_get_level(pins[i])!=0;
            samples++;
            esp_rom_delay_us(137);
        }
        for(unsigned i=0;i<PIN_COUNT;i++) ESP_LOGI(TAG,"GPIO%d pad samples: high=%u low=%u",pins[i],high[i],samples-high[i]);
        vTaskDelay(pdMS_TO_TICKS(9000));
    }
#endif
}
