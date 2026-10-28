#include "mpu6050.h"
#include "peripheral_resources.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdlib.h>

#define REG_SAMPLE_RATE 0x19
#define REG_CONFIG 0x1a
#define REG_GYRO_CONFIG 0x1b
#define REG_ACCEL_CONFIG 0x1c
#define REG_ACCEL_XOUT 0x3b
#define REG_POWER_1 0x6b
#define REG_WHO_AM_I 0x75
#define STANDARD_GRAVITY 9.80665f

struct mpu6050_instance {
    mpu6050_config_t config;
    peripheral_claim_t claim;
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t device;
    SemaphoreHandle_t lock;
    bool sleeping;
};

static esp_err_t write_reg(mpu6050_handle_t sensor, uint8_t reg, uint8_t value)
{
    uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(sensor->device, bytes, sizeof(bytes), (int)sensor->config.timeout_ms);
}

static esp_err_t configure_sensor(mpu6050_handle_t sensor)
{
    uint8_t reg = REG_WHO_AM_I, id = 0;
    esp_err_t result = i2c_master_transmit_receive(sensor->device, &reg, 1, &id, 1,
                                                 (int)sensor->config.timeout_ms);
    if (result != ESP_OK) return result;
    if (id != 0x68) return ESP_ERR_NOT_SUPPORTED;
    result = write_reg(sensor, REG_POWER_1, 0x80);
    if (result != ESP_OK) return result;
    vTaskDelay(pdMS_TO_TICKS(100));
    // Enable all axes and use the X-axis gyroscope PLL as the clock reference.
    const uint8_t settings[][2] = {
        {REG_POWER_1, 0x01}, {0x6c, 0},
        {REG_CONFIG, sensor->config.dlpf},
        {REG_SAMPLE_RATE, sensor->config.sample_rate_divider},
        {REG_GYRO_CONFIG, (uint8_t)(sensor->config.gyro_range << 3)},
        {REG_ACCEL_CONFIG, (uint8_t)(sensor->config.accel_range << 3)},
    };
    for (unsigned i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        result = write_reg(sensor, settings[i][0], settings[i][1]);
        if (result != ESP_OK) return result;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t mpu6050_create(const mpu6050_config_t *config, mpu6050_handle_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (!config || config->i2c_port < 0 || config->i2c_port >= (int)SOC_HP_I2C_NUM ||
        !GPIO_IS_VALID_OUTPUT_GPIO(config->sda_gpio) || !GPIO_IS_VALID_OUTPUT_GPIO(config->scl_gpio) ||
        config->sda_gpio == config->scl_gpio ||
        (config->address && config->address != 0x68 && config->address != 0x69) ||
        config->clock_hz > 400000 || (config->clock_hz && config->clock_hz < 10000) ||
        config->timeout_ms > 10000 || config->accel_range < MPU6050_ACCEL_2G ||
        config->accel_range > MPU6050_ACCEL_16G || config->gyro_range < MPU6050_GYRO_250DPS ||
        config->gyro_range > MPU6050_GYRO_2000DPS || config->dlpf > 6) return ESP_ERR_INVALID_ARG;
    mpu6050_handle_t sensor = calloc(1, sizeof(*sensor));
    if (!sensor) return ESP_ERR_NO_MEM;
    sensor->config = *config;
    if (!sensor->config.address) sensor->config.address = 0x68;
    if (!sensor->config.clock_hz) sensor->config.clock_hz = 400000;
    if (!sensor->config.timeout_ms) sensor->config.timeout_ms = 100;
    sensor->lock = xSemaphoreCreateMutex();
    if (!sensor->lock) { free(sensor); return ESP_ERR_NO_MEM; }
    sensor->claim = (peripheral_claim_t){
        .gpios=(UINT64_C(1) << config->sda_gpio) | (UINT64_C(1) << config->scl_gpio),
        .ledc_timer=-1, .ledc_channel=-1, .i2c_ports=1U << config->i2c_port,
    };
    esp_err_t result = peripheral_resources_acquire(&sensor->claim);
    if (result != ESP_OK) goto fail;
    i2c_master_bus_config_t bus_config = {
        .i2c_port=config->i2c_port, .sda_io_num=config->sda_gpio, .scl_io_num=config->scl_gpio,
        .clk_source=I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=config->internal_pullups,
    };
    result = i2c_new_master_bus(&bus_config, &sensor->bus);
    if (result != ESP_OK) goto release_claim;
    i2c_device_config_t device_config = {.dev_addr_length=I2C_ADDR_BIT_LEN_7,
        .device_address=sensor->config.address, .scl_speed_hz=sensor->config.clock_hz};
    result = i2c_master_bus_add_device(sensor->bus, &device_config, &sensor->device);
    if (result == ESP_OK) result = configure_sensor(sensor);
    if (result != ESP_OK) {
        if (sensor->device) i2c_master_bus_rm_device(sensor->device);
        i2c_del_master_bus(sensor->bus);
        gpio_reset_pin(config->sda_gpio);
        gpio_reset_pin(config->scl_gpio);
        goto release_claim;
    }
    *out = sensor;
    return ESP_OK;
release_claim:
    peripheral_resources_release(&sensor->claim);
fail:
    vSemaphoreDelete(sensor->lock);
    free(sensor);
    return result;
}

static int16_t signed_word(const uint8_t *bytes)
{
    uint16_t value = ((uint16_t)bytes[0] << 8) | bytes[1];
    return value < 0x8000 ? (int16_t)value : (int16_t)((int32_t)value - 65536);
}

esp_err_t mpu6050_read(mpu6050_handle_t sensor, mpu6050_sample_t *out)
{
    if (!sensor || !out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(sensor->lock, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (!sensor->sleeping && sensor->device) {
        uint8_t reg = REG_ACCEL_XOUT, bytes[14];
        result = i2c_master_transmit_receive(sensor->device, &reg, 1, bytes, sizeof(bytes),
                                            (int)sensor->config.timeout_ms);
        if (result == ESP_OK) {
            static const float accel_lsb[] = {16384, 8192, 4096, 2048};
            static const float gyro_lsb[] = {131, 65.5f, 32.8f, 16.4f};
            mpu6050_sample_t sample = {.timestamp_us=esp_timer_get_time()};
            for (unsigned i = 0; i < 3; ++i) {
                sample.raw_accel[i] = signed_word(bytes + 2*i);
                sample.raw_gyro[i] = signed_word(bytes + 8 + 2*i);
                sample.accel_m_s2[i] = sample.raw_accel[i] * STANDARD_GRAVITY / accel_lsb[sensor->config.accel_range];
                sample.gyro_dps[i] = sample.raw_gyro[i] / gyro_lsb[sensor->config.gyro_range];
            }
            sample.raw_temperature = signed_word(bytes + 6);
            sample.temperature_c = sample.raw_temperature / 340.0f + 36.53f;
            *out = sample;
        }
    }
    xSemaphoreGive(sensor->lock);
    return result;
}

esp_err_t mpu6050_set_sleep(mpu6050_handle_t sensor, bool sleeping)
{
    if (!sensor) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(sensor->lock, portMAX_DELAY);
    esp_err_t result = ESP_OK;
    if (!sensor->device) result = ESP_ERR_INVALID_STATE;
    else if (sensor->sleeping != sleeping) {
        result = write_reg(sensor, REG_POWER_1, sleeping ? 0x41 : 0x01);
        if (result == ESP_OK) {
            sensor->sleeping = sleeping;
            if (!sleeping) vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    xSemaphoreGive(sensor->lock);
    return result;
}

esp_err_t mpu6050_destroy(mpu6050_handle_t sensor)
{
    if (!sensor) return ESP_ERR_INVALID_ARG;
    if (sensor->device) {
        esp_err_t result = i2c_master_bus_rm_device(sensor->device);
        if (result != ESP_OK) return result;
        sensor->device = NULL;
    }
    esp_err_t result = i2c_del_master_bus(sensor->bus);
    if (result != ESP_OK) return result;
    gpio_reset_pin(sensor->config.sda_gpio);
    gpio_reset_pin(sensor->config.scl_gpio);
    peripheral_resources_release(&sensor->claim);
    vSemaphoreDelete(sensor->lock);
    free(sensor);
    return ESP_OK;
}
