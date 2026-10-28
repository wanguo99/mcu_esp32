// Shared behavioral checks against the real worker on both PWM backends.
static servo_status_t release_status(servo_handle_t servo) {
    servo_status_t s; assert(servo_get_status(servo,&s)==ESP_OK); return s;
}
static void await_release(servo_handle_t servo) {
    int64_t deadline=esp_timer_get_time()+500000;
    while(release_status(servo).output_enabled) {
        assert(esp_timer_get_time()<deadline); vTaskDelay(5);
    }
    assert(release_status(servo).auto_released && !release_status(servo).moving);
}
static void automatic_release(void) {
#if CONFIG_IDF_TARGET_ESP32
    servo_config_t config=SERVO_CONFIG_DEFAULT(14,1,1);
#else
    servo_config_t config=SERVO_CONFIG_DEFAULT(1,1,1);
#endif
    config.auto_release_ms=80;
    servo_handle_t servo;
    assert(servo_create(&config,&servo)==ESP_OK);
    assert(!release_status(servo).auto_released);
    assert(servo_set_angle(servo,90)==ESP_OK);
    await_release(servo);
    assert(release_status(servo).angle_known && release_status(servo).commanded_angle==90);
    assert(servo_record_home(servo)==ESP_OK && !release_status(servo).output_enabled);

    // Release must not interrupt a trajectory longer than the release delay.
    assert(servo_move_to(servo,120,140,SERVO_CURVE_SMOOTH)==ESP_OK);
    vTaskDelay(100);
    assert(release_status(servo).moving && release_status(servo).output_enabled);
    assert(servo_wait_until_idle(servo,400)==ESP_OK);
    assert(release_status(servo).output_enabled && release_status(servo).commanded_angle==120);
    await_release(servo);
    assert(servo_move_by(servo,-10,40,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(servo,400)==ESP_OK);
    await_release(servo);
    assert(release_status(servo).commanded_angle==110);
    assert(servo_return_home(servo,40,SERVO_CURVE_LINEAR)==ESP_OK);
    assert(servo_wait_until_idle(servo,400)==ESP_OK);
    await_release(servo);
    assert(release_status(servo).commanded_angle==90);

    // A new direct command replaces the pending release deadline.
    assert(servo_set_angle(servo,60)==ESP_OK); vTaskDelay(50);
    assert(servo_set_angle(servo,90)==ESP_OK); vTaskDelay(50);
    assert(release_status(servo).output_enabled);
    await_release(servo);

    servo_step_t step={.angle=60,.duration_ms=40,.hold_ms=150};
    assert(servo_play_sequence(servo,&step,1,1,SERVO_CURVE_LINEAR)==ESP_OK);
    vTaskDelay(120);
    assert(release_status(servo).moving && release_status(servo).output_enabled);
    assert(servo_wait_until_idle(servo,400)==ESP_OK);
    await_release(servo);

    // Explicit stop stays distinct from automatic release.
    assert(servo_stop(servo)==ESP_OK && !release_status(servo).auto_released);
    assert(servo_move_by(servo,10,40,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(servo_return_home(servo,40,SERVO_CURVE_LINEAR)==ESP_ERR_INVALID_STATE);
    assert(servo_set_angle(servo,90)==ESP_OK);
    assert(servo_destroy(servo)==ESP_OK); // Also cancels a pending release safely.
}
