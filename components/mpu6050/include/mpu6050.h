#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct mpu6050_instance *mpu6050_handle_t;
typedef enum {
    MPU6050_ACCEL_2G, MPU6050_ACCEL_4G, MPU6050_ACCEL_8G, MPU6050_ACCEL_16G,
} mpu6050_accel_range_t;
typedef enum {
    MPU6050_GYRO_250DPS, MPU6050_GYRO_500DPS, MPU6050_GYRO_1000DPS, MPU6050_GYRO_2000DPS,
} mpu6050_gyro_range_t;
typedef struct {
    int i2c_port, sda_gpio, scl_gpio; // Owns one bus; do not share with the camera.
    uint8_t address; // 0 selects 0x68; AD0 high selects 0x69. WHO_AM_I remains 0x68.
    uint32_t clock_hz, timeout_ms; // 0 selects 400 kHz / 100 ms.
    mpu6050_accel_range_t accel_range;
    mpu6050_gyro_range_t gyro_range;
    uint8_t dlpf; // Register DLPF_CFG: 0..6. 7 is reserved.
    uint8_t sample_rate_divider; // Register SMPLRT_DIV: 0..255.
    bool internal_pullups; // External I2C pull-ups are recommended.
} mpu6050_config_t;

#define MPU6050_CONFIG_DEFAULT(sda_pin, scl_pin) { \
    .i2c_port=0, .sda_gpio=(sda_pin), .scl_gpio=(scl_pin), .address=0x68, \
    .clock_hz=400000, .timeout_ms=100, .accel_range=MPU6050_ACCEL_2G, \
    .gyro_range=MPU6050_GYRO_250DPS, .dlpf=3, .sample_rate_divider=9, .internal_pullups=false \
}

typedef struct {
    int16_t raw_accel[3], raw_gyro[3], raw_temperature;
    float accel_m_s2[3], gyro_dps[3], temperature_c;
    int64_t timestamp_us; // MCU monotonic time after the coherent 14-byte burst.
} mpu6050_sample_t;

// Verifies WHO_AM_I, resets, wakes and configures the sensor (~200 ms).
esp_err_t mpu6050_create(const mpu6050_config_t *config, mpu6050_handle_t *out);
// Reads the latest sample (may repeat if called faster than the sample rate).
// On error, leaves *out unchanged. Acceleration includes gravity.
esp_err_t mpu6050_read(mpu6050_handle_t sensor, mpu6050_sample_t *out);
esp_err_t mpu6050_set_sleep(mpu6050_handle_t sensor, bool sleeping);
// Detaches the bus; call set_sleep explicitly if the sensor should be put to sleep.
// Caller must exclude concurrent users before destruction. Normal task context only.
esp_err_t mpu6050_destroy(mpu6050_handle_t sensor);
