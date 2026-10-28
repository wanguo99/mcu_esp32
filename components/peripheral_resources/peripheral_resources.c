#include "peripheral_resources.h"
#include "board.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static uint64_t s_gpios;
static uint32_t s_channels;
static uint32_t s_i2c_ports;
static struct { unsigned users, bits; uint32_t hz; } s_timers[LEDC_TIMER_MAX];

static uint64_t pin(int gpio) { return gpio >= 0 && gpio < 64 ? UINT64_C(1) << gpio : 0; }

static bool conflicts_with_board(const peripheral_claim_t *claim)
{
    const board_config_t *board = board_get_config();
    uint64_t reserved = 0;
#if CONFIG_IDF_TARGET_ESP32
    for (int gpio = 6; gpio <= 11; ++gpio) reserved |= pin(gpio); // Module flash bus.
    if (board->psram_mb) reserved |= pin(16) | pin(17);
#else
    reserved |= pin(19) | pin(20); // ESP32-S3 native USB Serial/JTAG.
    for (int gpio = 26; gpio <= 32; ++gpio) reserved |= pin(gpio); // Flash bus.
    if (board->psram_mb == 8)
        for (int gpio = 33; gpio <= 37; ++gpio) reserved |= pin(gpio); // Octal PSRAM.
#endif
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
#if CONFIG_IDF_TARGET_ESP32
    reserved |= pin(1) | pin(3);
#else
    reserved |= pin(43) | pin(44);
#endif
#elif CONFIG_ESP_CONSOLE_UART_CUSTOM
    reserved |= pin(CONFIG_ESP_CONSOLE_UART_TX_GPIO) | pin(CONFIG_ESP_CONSOLE_UART_RX_GPIO);
#endif
#if CONFIG_MCU_ENABLE_CAMERA
    const board_camera_pins_t *camera = board->camera;
    if (camera) {
#if CONFIG_SCCB_HARDWARE_I2C_PORT1
        if (claim->i2c_ports & (1U << 1)) return true;
#else
        if (claim->i2c_ports & 1U) return true;
#endif
        reserved |= pin(camera->pwdn) | pin(camera->reset) | pin(camera->xclk) |
                    pin(camera->sda) | pin(camera->scl) | pin(camera->vsync) |
                    pin(camera->href) | pin(camera->pclk);
        for (int i = 0; i < 8; ++i) reserved |= pin(camera->d[i]);
        if (claim->ledc_timer >= 0 && (claim->ledc_timer == camera->ledc_timer ||
                                     claim->ledc_channel == camera->ledc_channel)) return true;
    }
#endif
#if CONFIG_MCU_ENABLE_STORAGE
    if (board->sd) reserved |= pin(board->sd->clk) | pin(board->sd->cmd) | pin(board->sd->d0);
#endif
    return (claim->gpios & reserved) != 0;
}

esp_err_t peripheral_resources_acquire(const peripheral_claim_t *claim)
{
    if (!claim || !claim->gpios || (claim->gpios & ~SOC_GPIO_VALID_GPIO_MASK) ||
        (claim->i2c_ports & ~((1U << SOC_HP_I2C_NUM) - 1)) ||
        claim->ledc_timer < -1 || claim->ledc_timer >= LEDC_TIMER_MAX ||
        claim->ledc_channel < -1 || claim->ledc_channel >= LEDC_CHANNEL_MAX ||
        ((claim->ledc_timer < 0) != (claim->ledc_channel < 0)) ||
        (claim->ledc_timer >= 0 && (!claim->frequency_hz || !claim->resolution_bits)) ||
        conflicts_with_board(claim)) {
        ESP_LOGE("resources", "Invalid or reserved resources; check GPIO, LEDC and camera SCCB I2C configuration");
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lock);
    bool busy = (s_gpios & claim->gpios) != 0 || (s_i2c_ports & claim->i2c_ports) != 0;
    if (claim->ledc_timer >= 0) {
        const int timer = claim->ledc_timer;
        busy |= (s_channels & (1U << claim->ledc_channel)) != 0 ||
                (s_timers[timer].users && (s_timers[timer].hz != claim->frequency_hz ||
                                         s_timers[timer].bits != claim->resolution_bits));
    }
    if (!busy) {
        s_gpios |= claim->gpios;
        s_i2c_ports |= claim->i2c_ports;
        if (claim->ledc_timer >= 0) {
            s_channels |= 1U << claim->ledc_channel;
            ++s_timers[claim->ledc_timer].users;
            s_timers[claim->ledc_timer].hz = claim->frequency_hz;
            s_timers[claim->ledc_timer].bits = claim->resolution_bits;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    if (busy) ESP_LOGE("resources", "Resources already owned: GPIO mask=0x%llx LEDC timer=%d channel=%d I2C mask=0x%lx",
        (unsigned long long)claim->gpios, claim->ledc_timer, claim->ledc_channel, (unsigned long)claim->i2c_ports);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void peripheral_resources_release(const peripheral_claim_t *claim)
{
    portENTER_CRITICAL(&s_lock);
    s_gpios &= ~claim->gpios;
    s_i2c_ports &= ~claim->i2c_ports;
    if (claim->ledc_timer >= 0) {
        s_channels &= ~(1U << claim->ledc_channel);
        --s_timers[claim->ledc_timer].users;
    }
    portEXIT_CRITICAL(&s_lock);
}
