#include "servo.h"
#include "peripheral_resources.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#if CONFIG_IDF_TARGET_ESP32
#include "driver/mcpwm_prelude.h"
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SERVO_MAX_STEPS 32
#define SERVO_MAX_TIME_MS 600000U
struct servo_instance {
    servo_config_t config;
    peripheral_claim_t claim;
    SemaphoreHandle_t lock, done;
    TaskHandle_t worker;
    bool closing;
    servo_status_t status;
    servo_step_t steps[SERVO_MAX_STEPS];
    uint32_t count, repeats;
    servo_curve_t curve;
    float from;
    int64_t started_us;
    int64_t release_at_us;
#if CONFIG_IDF_TARGET_ESP32
    mcpwm_timer_handle_t pwm_timer;
    mcpwm_oper_handle_t pwm_operator;
    mcpwm_cmpr_handle_t pwm_comparator;
    mcpwm_gen_handle_t pwm_generator;
    bool pwm_timer_enabled, pwm_timer_started;
    uint32_t pwm_compare;
#endif
};

static bool calibrated_angle(servo_handle_t servo, float angle)
{ return isfinite(angle) && angle >= servo->config.min_angle && angle <= servo->config.max_angle; }
// Caller holds the instance lock: runtime limits can change.
static bool valid_angle(servo_handle_t servo, float angle)
{ return isfinite(angle) && angle >= servo->status.min_angle && angle <= servo->status.max_angle; }
static bool valid_curve(servo_curve_t curve)
{ return curve == SERVO_CURVE_LINEAR || curve == SERVO_CURVE_SMOOTH; }
static TickType_t ticks(uint32_t ms)
{ TickType_t value = pdMS_TO_TICKS(ms); return value ? value : 1; }

static void motion_task(void *arg);
static esp_err_t ensure_worker_locked(servo_handle_t servo)
{
    if (!servo->worker && xTaskCreate(motion_task, "servo_motion", 3072, servo, 4, &servo->worker) != pdPASS)
        return ESP_ERR_NO_MEM;
    return ESP_OK;
}
static bool command_available(servo_handle_t servo)
{ return servo->status.angle_known && (servo->status.output_enabled || servo->status.auto_released); }

static esp_err_t stop_locked(servo_handle_t servo, bool automatic)
{
    servo->status.moving = false;
    servo->release_at_us = 0;
#if CONFIG_IDF_TARGET_ESP32
    esp_err_t result = servo->pwm_generator ? mcpwm_generator_set_force_level(servo->pwm_generator,0,true) : ESP_OK;
#else
    esp_err_t result = ledc_stop(LEDC_LOW_SPEED_MODE,servo->config.channel,0);
#endif
    // Releasing a failed trajectory must not erase its failure report.
    if (!automatic || result != ESP_OK) servo->status.last_error = result;
    servo->status.auto_released = automatic && result == ESP_OK;
    if (result == ESP_OK) servo->status.output_enabled = false;
    return result;
}

#if CONFIG_IDF_TARGET_ESP32
static esp_err_t pwm_cleanup(servo_handle_t servo)
{
    esp_err_t err;
    if(servo->pwm_timer_started) {
        err=mcpwm_timer_start_stop(servo->pwm_timer,MCPWM_TIMER_STOP_EMPTY);
        if(err!=ESP_OK) return err;
        servo->pwm_timer_started=false;
        // Stop is latched at a timer boundary; output has already been forced low.
        vTaskDelay(ticks((2000U+servo->config.frequency_hz-1)/servo->config.frequency_hz+10));
    }
    if(servo->pwm_timer_enabled) {
        err=mcpwm_timer_disable(servo->pwm_timer);
        if(err!=ESP_OK) return err;
        servo->pwm_timer_enabled=false;
    }
    if(servo->pwm_generator) {
        err=mcpwm_del_generator(servo->pwm_generator);
        if(err!=ESP_OK) return err;
        servo->pwm_generator=NULL;
    }
    if(servo->pwm_comparator) {
        err=mcpwm_del_comparator(servo->pwm_comparator);
        if(err!=ESP_OK) return err;
        servo->pwm_comparator=NULL;
    }
    if(servo->pwm_operator) {
        err=mcpwm_del_operator(servo->pwm_operator);
        if(err!=ESP_OK) return err;
        servo->pwm_operator=NULL;
    }
    if(servo->pwm_timer) {
        err=mcpwm_del_timer(servo->pwm_timer);
        if(err!=ESP_OK) return err;
        servo->pwm_timer=NULL;
    }
    return ESP_OK;
}

static esp_err_t pwm_initialize(servo_handle_t servo)
{
    esp_err_t err=ESP_ERR_NOT_FOUND;
    // Original ESP32 has two groups, each with three timers/operators.
    for(int group=0;group<2;group++) {
        mcpwm_timer_config_t timer={.group_id=group,.clk_src=MCPWM_TIMER_CLK_SRC_DEFAULT,
            .resolution_hz=1000000,.period_ticks=1000000U/servo->config.frequency_hz,
            .count_mode=MCPWM_TIMER_COUNT_MODE_UP};
        err=mcpwm_new_timer(&timer,&servo->pwm_timer);
        if(err==ESP_ERR_NOT_FOUND) continue;
        if(err!=ESP_OK) return err;
        mcpwm_operator_config_t oper={.group_id=group};
        err=mcpwm_new_operator(&oper,&servo->pwm_operator);
        if(err==ESP_OK) break;
        esp_err_t cleanup=pwm_cleanup(servo);
        if(cleanup!=ESP_OK) return cleanup;
        if(err!=ESP_ERR_NOT_FOUND) return err;
    }
    if(err!=ESP_OK) return err;
    err=mcpwm_operator_connect_timer(servo->pwm_operator,servo->pwm_timer);
    mcpwm_comparator_config_t cmp={.flags.update_cmp_on_tez=true};
    if(err==ESP_OK) err=mcpwm_new_comparator(servo->pwm_operator,&cmp,&servo->pwm_comparator);
    mcpwm_generator_config_t gen={.gen_gpio_num=servo->config.gpio};
    if(err==ESP_OK) err=mcpwm_new_generator(servo->pwm_operator,&gen,&servo->pwm_generator);
    if(err==ESP_OK) err=mcpwm_generator_set_force_level(servo->pwm_generator,0,true);
    if(err==ESP_OK) err=mcpwm_comparator_set_compare_value(servo->pwm_comparator,servo->config.min_pulse_us);
    if(err==ESP_OK) err=mcpwm_generator_set_action_on_timer_event(servo->pwm_generator,
        MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,MCPWM_TIMER_EVENT_EMPTY,MCPWM_GEN_ACTION_HIGH));
    if(err==ESP_OK) err=mcpwm_generator_set_action_on_compare_event(servo->pwm_generator,
        MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,servo->pwm_comparator,MCPWM_GEN_ACTION_LOW));
    if(err==ESP_OK) err=gpio_input_enable(servo->config.gpio);
    if(err==ESP_OK) {
        err=mcpwm_timer_enable(servo->pwm_timer);
        servo->pwm_timer_enabled=err==ESP_OK;
    }
    if(err==ESP_OK) {
        err=mcpwm_timer_start_stop(servo->pwm_timer,MCPWM_TIMER_START_NO_STOP);
        servo->pwm_timer_started=err==ESP_OK;
    }
    return err;
}
#endif

static esp_err_t apply_locked(servo_handle_t servo, float angle)
{
    const servo_config_t *config = &servo->config;
    double position = (double)(angle-config->min_angle)/(config->max_angle-config->min_angle);
    if (config->reversed) position = 1.0-position;
    double pulse = config->min_pulse_us + position*(config->max_pulse_us-config->min_pulse_us);
#if CONFIG_IDF_TARGET_ESP32
    uint32_t compare=(uint32_t)(pulse+0.5);
    esp_err_t result=mcpwm_comparator_set_compare_value(servo->pwm_comparator,compare);
    if(result==ESP_OK && !servo->status.output_enabled)
        result=mcpwm_generator_set_force_level(servo->pwm_generator,-1,true);
    if(result==ESP_OK) servo->pwm_compare=compare;
#else
    uint32_t duty = (uint32_t)(pulse * config->frequency_hz * 16384.0 / 1000000.0 + 0.5);
    // This channel is exclusively owned and the instance lock serializes updates.
    // The combined API requires the global LEDC fade service, which we do not use.
    esp_err_t result = ledc_set_duty(LEDC_LOW_SPEED_MODE, config->channel, duty);
    if (result == ESP_OK) result = ledc_update_duty(LEDC_LOW_SPEED_MODE, config->channel);
#endif
    servo->status.last_error = result;
    if (result == ESP_OK) {
        servo->status.output_enabled = servo->status.angle_known = true;
        servo->status.auto_released = false;
        servo->status.commanded_angle = angle;
        servo->release_at_us = config->auto_release_ms ? esp_timer_get_time()+(int64_t)config->auto_release_ms*1000 : 0;
    } else servo->status.moving = false;
    return result;
}

static void update_locked(servo_handle_t servo)
{
    if (!servo->status.moving) return;
    servo_step_t *step = &servo->steps[servo->status.step_index];
    uint64_t elapsed = (uint64_t)(esp_timer_get_time()-servo->started_us)/1000;
    float position = elapsed >= step->duration_ms ? 1.0f : (float)elapsed/step->duration_ms;
    if (servo->curve == SERVO_CURVE_SMOOTH) position = position*position*(3-2*position);
    float angle = servo->from + (step->angle-servo->from)*position;
    if (elapsed >= step->duration_ms) angle = step->angle;
    if (apply_locked(servo, angle) != ESP_OK) return;
    if (elapsed < (uint64_t)step->duration_ms + step->hold_ms) return;
    if (++servo->status.step_index == servo->count) {
        if (servo->repeats == 1) { servo->status.moving = false; return; }
        if (servo->repeats) --servo->repeats;
        servo->status.step_index = 0;
    }
    servo->from = servo->status.commanded_angle;
    servo->status.target_angle = servo->steps[servo->status.step_index].angle;
    servo->started_us = esp_timer_get_time();
}

static void motion_task(void *arg)
{
    servo_handle_t servo = arg;
    for (;;) {
        xSemaphoreTake(servo->lock, portMAX_DELAY);
        bool closing = servo->closing;
        if (!closing) {
            update_locked(servo);
            if (!servo->status.moving && servo->release_at_us && esp_timer_get_time() >= servo->release_at_us)
                stop_locked(servo,true);
        }
        TickType_t wait = portMAX_DELAY;
        if (servo->status.moving) wait = ticks(servo->config.refresh_interval_ms);
        else if (servo->release_at_us) {
            int64_t remaining = servo->release_at_us-esp_timer_get_time();
            wait = ticks(remaining > 0 ? (uint32_t)((remaining+999)/1000) : 1);
        }
        xSemaphoreGive(servo->lock);
        if (closing) break;
        ulTaskNotifyTake(pdTRUE, wait);
    }
    xSemaphoreGive(servo->done);
    vTaskDelete(NULL);
}

esp_err_t servo_create(const servo_config_t *config, servo_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || !GPIO_IS_VALID_OUTPUT_GPIO(config->gpio) || config->timer < 0 || config->timer >= LEDC_TIMER_MAX ||
        config->channel < 0 || config->channel >= LEDC_CHANNEL_MAX || !config->frequency_hz ||
        config->frequency_hz > 1000 || !config->min_pulse_us || config->min_pulse_us >= config->max_pulse_us ||
        (uint64_t)config->max_pulse_us * config->frequency_hz >= 1000000 ||
        !isfinite(config->min_angle) || !isfinite(config->max_angle) ||
        !isfinite(config->max_angle-config->min_angle) || config->min_angle >= config->max_angle ||
        config->auto_release_ms > SERVO_MAX_TIME_MS ||
        (config->refresh_interval_ms && (config->refresh_interval_ms < 5 || config->refresh_interval_ms > 1000)))
        return ESP_ERR_INVALID_ARG;
    servo_handle_t servo = calloc(1, sizeof(*servo));
    if (!servo) return ESP_ERR_NO_MEM;
    servo->config = *config;
    servo->status.min_angle = config->min_angle;
    servo->status.max_angle = config->max_angle;
    if (!servo->config.refresh_interval_ms) servo->config.refresh_interval_ms = 20;
    servo->lock = xSemaphoreCreateMutex();
    servo->done = xSemaphoreCreateBinary();
    esp_err_t result = ESP_ERR_NO_MEM;
    if (!servo->lock || !servo->done) goto fail;
    servo->claim = (peripheral_claim_t){
        .gpios=UINT64_C(1) << config->gpio, .ledc_timer=config->timer,
        .ledc_channel=config->channel, .frequency_hz=config->frequency_hz, .resolution_bits=14,
    };
#if CONFIG_IDF_TARGET_ESP32
    // MCPWM allocates timer/operator resources through its driver, independently of LEDC.
    servo->claim.ledc_timer=servo->claim.ledc_channel=-1;
#endif
    result = peripheral_resources_acquire(&servo->claim);
    if (result != ESP_OK) goto fail;
    // Match the bench application's deterministic pad setup. Leave output-path
    // ownership to LEDC instead of reserving it first through gpio_config().
    result = gpio_reset_pin(config->gpio);
    if (result == ESP_OK) result = gpio_set_level(config->gpio, 0);
    if (result == ESP_OK) result = gpio_set_pull_mode(config->gpio, GPIO_FLOATING);
    if (result == ESP_OK) result = gpio_input_enable(config->gpio);
#if CONFIG_IDF_TARGET_ESP32
    if(result==ESP_OK) result=pwm_initialize(servo);
    if(result!=ESP_OK) pwm_cleanup(servo);
#else
    ledc_timer_config_t timer = {.speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=config->timer,
        .duty_resolution=LEDC_TIMER_14_BIT, .freq_hz=config->frequency_hz, .clk_cfg=LEDC_AUTO_CLK};
    if (result == ESP_OK) result = ledc_timer_config(&timer);
    if (result == ESP_OK) {
        ledc_channel_config_t channel = {.gpio_num=config->gpio, .speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=config->channel, .timer_sel=config->timer, .duty=0, .hpoint=0};
        result = ledc_channel_config(&channel);
    }
#endif
    if (result != ESP_OK) { gpio_reset_pin(config->gpio); peripheral_resources_release(&servo->claim); goto fail; }
    *out = servo;
    return ESP_OK;
fail:
    if (servo->done) vSemaphoreDelete(servo->done);
    if (servo->lock) vSemaphoreDelete(servo->lock);
    free(servo);
    return result;
}

esp_err_t servo_set_angle(servo_handle_t servo, float angle)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    if (!valid_angle(servo, angle)) { xSemaphoreGive(servo->lock); return ESP_ERR_INVALID_ARG; }
    if (servo->closing) { xSemaphoreGive(servo->lock); return ESP_ERR_INVALID_STATE; }
    if (servo->config.auto_release_ms && ensure_worker_locked(servo) != ESP_OK) {
        xSemaphoreGive(servo->lock); return ESP_ERR_NO_MEM;
    }
    servo->status.moving = false;
    esp_err_t result = apply_locked(servo, angle);
    if (result == ESP_OK) servo->status.target_angle = angle;
    if (servo->worker) xTaskNotifyGive(servo->worker);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_set_pulse_us(servo_handle_t servo, uint32_t pulse_us)
{
    if (!servo || pulse_us < servo->config.min_pulse_us || pulse_us > servo->config.max_pulse_us)
        return ESP_ERR_INVALID_ARG;
    double position = (double)(pulse_us-servo->config.min_pulse_us)/
        (servo->config.max_pulse_us-servo->config.min_pulse_us);
    if (servo->config.reversed) position = 1.0-position;
    float angle = servo->config.min_angle + position*(servo->config.max_angle-servo->config.min_angle);
    return servo_set_angle(servo, angle);
}

static esp_err_t submit_locked(servo_handle_t servo, const servo_step_t *steps, uint32_t count,
                               uint32_t repeat_count, servo_curve_t curve)
{
    if (servo->closing || !command_available(servo)) return ESP_ERR_INVALID_STATE;
    esp_err_t result = ensure_worker_locked(servo);
    if (result != ESP_OK) return result;
    // Preserve the current interpolated command when replacing an active sequence.
    update_locked(servo);
    if (servo->status.last_error != ESP_OK) return servo->status.last_error;
    if (!servo->status.output_enabled && (result=apply_locked(servo,servo->status.commanded_angle)) != ESP_OK)
        return result;
    memcpy(servo->steps, steps, count*sizeof(*steps));
    servo->count = count; servo->repeats = repeat_count; servo->curve = curve;
    servo->from = servo->status.commanded_angle; servo->started_us = esp_timer_get_time();
    servo->status.moving = true; servo->status.step_index = 0;
    servo->status.target_angle = steps[0].angle;
    xTaskNotifyGive(servo->worker);
    return ESP_OK;
}

esp_err_t servo_play_sequence(servo_handle_t servo, const servo_step_t *steps, uint32_t count,
                               uint32_t repeat_count, servo_curve_t curve)
{
    if (!servo || !steps || !count || count > SERVO_MAX_STEPS || !valid_curve(curve)) return ESP_ERR_INVALID_ARG;
    for (uint32_t i=0; i<count; ++i)
        if (!calibrated_angle(servo, steps[i].angle) || !steps[i].duration_ms ||
            steps[i].duration_ms > SERVO_MAX_TIME_MS || steps[i].hold_ms > SERVO_MAX_TIME_MS)
            return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    for (uint32_t i=0; i<count; ++i) {
        if (!valid_angle(servo, steps[i].angle)) { xSemaphoreGive(servo->lock); return ESP_ERR_INVALID_ARG; }
    }
    esp_err_t result = submit_locked(servo, steps, count, repeat_count, curve);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_move_to(servo_handle_t servo, float angle, uint32_t duration_ms, servo_curve_t curve)
{
    if (!valid_curve(curve)) return ESP_ERR_INVALID_ARG;
    if (!duration_ms) return servo_set_angle(servo, angle);
    servo_step_t step = {.angle=angle, .duration_ms=duration_ms};
    return servo_play_sequence(servo, &step, 1, 1, curve);
}

static esp_err_t move_locked(servo_handle_t servo, float angle, uint32_t duration_ms, servo_curve_t curve)
{
    if (!valid_angle(servo,angle)) return ESP_ERR_INVALID_ARG;
    if (!duration_ms) {
        servo->status.moving = false;
        esp_err_t result = apply_locked(servo,angle);
        if (result == ESP_OK) servo->status.target_angle = angle;
        return result;
    }
    servo_step_t step = {.angle=angle,.duration_ms=duration_ms};
    return submit_locked(servo,&step,1,1,curve);
}

esp_err_t servo_move_by(servo_handle_t servo, float delta_angle, uint32_t duration_ms, servo_curve_t curve)
{
    if (!servo || !isfinite(delta_angle) || duration_ms > SERVO_MAX_TIME_MS || !valid_curve(curve))
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock,portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!servo->closing && command_available(servo)) {
        // Use the last PWM command actually issued, not a pending target or physical measurement.
        result = move_locked(servo,servo->status.commanded_angle+delta_angle,duration_ms,curve);
    }
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_record_home(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock,portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!servo->closing && !servo->status.moving && command_available(servo) &&
        servo->status.last_error == ESP_OK) {
        servo->status.home_angle = servo->status.commanded_angle;
        servo->status.home_known = true;
        result = ESP_OK;
    }
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_return_home(servo_handle_t servo, uint32_t duration_ms, servo_curve_t curve)
{
    if (!servo || duration_ms > SERVO_MAX_TIME_MS || !valid_curve(curve)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock,portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!servo->closing && servo->status.home_known && command_available(servo))
        result = move_locked(servo,servo->status.home_angle,duration_ms,curve);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_set_limits(servo_handle_t servo, float min_angle, float max_angle)
{
    if (!servo || !calibrated_angle(servo,min_angle) || !calibrated_angle(servo,max_angle) || min_angle >= max_angle)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock,portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!servo->closing && !servo->status.moving &&
        (!servo->status.angle_known || (servo->status.commanded_angle >= min_angle && servo->status.commanded_angle <= max_angle)) &&
        (!servo->status.home_known || (servo->status.home_angle >= min_angle && servo->status.home_angle <= max_angle))) {
        servo->status.min_angle = min_angle;
        servo->status.max_angle = max_angle;
        result = ESP_OK;
    }
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_move_with_limits(servo_handle_t servo, float angle, float speed, float acceleration)
{
    if (!servo || !calibrated_angle(servo,angle) || !isfinite(speed) || !isfinite(acceleration) || speed<=0 || acceleration<=0)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    if (!valid_angle(servo,angle)) { xSemaphoreGive(servo->lock); return ESP_ERR_INVALID_ARG; }
    update_locked(servo);
    double distance = fabs((double)angle-servo->status.commanded_angle);
    double duration = ceil(fmax(1.5*distance/speed, sqrt(6*distance/acceleration))*1000);
    esp_err_t result = ESP_ERR_INVALID_ARG;
    if (duration <= SERVO_MAX_TIME_MS) {
        servo_step_t step = {.angle=angle, .duration_ms=duration < 1 ? 1 : (uint32_t)duration};
        result = submit_locked(servo, &step, 1, 1, SERVO_CURVE_SMOOTH);
    }
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_sweep(servo_handle_t servo, float min_angle, float max_angle, uint32_t travel_ms, uint32_t hold_ms)
{
    if (min_angle >= max_angle) return ESP_ERR_INVALID_ARG;
    servo_step_t steps[] = {{min_angle,travel_ms,hold_ms},{max_angle,travel_ms,hold_ms}};
    return servo_play_sequence(servo, steps, 2, 0, SERVO_CURVE_SMOOTH);
}

esp_err_t servo_get_status(servo_handle_t servo, servo_status_t *out)
{
    if (!servo || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    *out = servo->status;
    out->step_remaining_ms = 0;
    if (out->moving) {
        servo_step_t *step = &servo->steps[out->step_index];
        uint64_t total = (uint64_t)step->duration_ms+step->hold_ms;
        uint64_t elapsed = (uint64_t)(esp_timer_get_time()-servo->started_us)/1000;
        out->step_remaining_ms = elapsed >= total ? 0 : (uint32_t)(total-elapsed);
    }
    xSemaphoreGive(servo->lock);
    return ESP_OK;
}

esp_err_t servo_get_pwm_status(servo_handle_t servo,servo_pwm_status_t *out)
{
    if(!servo || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock,portMAX_DELAY);
#if CONFIG_IDF_TARGET_ESP32
    *out=(servo_pwm_status_t){.backend="MCPWM",.hardware_readback=false,
        .frequency_hz=servo->config.frequency_hz,.compare_ticks=servo->pwm_compare,
        .period_ticks=1000000U/servo->config.frequency_hz,.output_enabled=servo->status.output_enabled};
#else
    *out=(servo_pwm_status_t){.backend="LEDC",.hardware_readback=true,
        .frequency_hz=ledc_get_freq(LEDC_LOW_SPEED_MODE,servo->config.timer),
        .compare_ticks=ledc_get_duty(LEDC_LOW_SPEED_MODE,servo->config.channel),
        .period_ticks=16384,.output_enabled=servo->status.output_enabled};
#endif
    xSemaphoreGive(servo->lock);
    return ESP_OK;
}

esp_err_t servo_wait_until_idle(servo_handle_t servo, uint32_t timeout_ms)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    int64_t deadline = esp_timer_get_time()+(int64_t)timeout_ms*1000;
    for (;;) {
        servo_status_t status; servo_get_status(servo,&status);
        if (!status.moving) return status.last_error;
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(ticks(10));
    }
}

esp_err_t servo_cancel_motion(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    if (servo->closing) { xSemaphoreGive(servo->lock); return ESP_ERR_INVALID_STATE; }
    update_locked(servo); servo->status.moving = false;
    servo->status.target_angle = servo->status.commanded_angle;
    if (servo->status.output_enabled && servo->config.auto_release_ms)
        servo->release_at_us = esp_timer_get_time()+(int64_t)servo->config.auto_release_ms*1000;
    if (servo->worker) xTaskNotifyGive(servo->worker);
    esp_err_t result = servo->status.last_error;
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_stop(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    esp_err_t result = stop_locked(servo,false);
    if (servo->worker) xTaskNotifyGive(servo->worker);
    xSemaphoreGive(servo->lock);
    return result;
}

esp_err_t servo_destroy(servo_handle_t servo)
{
    if (!servo) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(servo->lock, portMAX_DELAY);
    servo->closing = true; servo->status.moving = false;
    if (servo->worker) xTaskNotifyGive(servo->worker);
    xSemaphoreGive(servo->lock);
    if (servo->worker) { xSemaphoreTake(servo->done, portMAX_DELAY); servo->worker = NULL; }
    esp_err_t result = servo_stop(servo);
    if (result != ESP_OK) return result;
#if CONFIG_IDF_TARGET_ESP32
    result=pwm_cleanup(servo);
    if(result!=ESP_OK) return result;
#endif
    gpio_reset_pin(servo->config.gpio);
    peripheral_resources_release(&servo->claim);
    vSemaphoreDelete(servo->done); vSemaphoreDelete(servo->lock); free(servo);
    return ESP_OK;
}
