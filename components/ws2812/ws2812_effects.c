#include "ws2812_effects.h"

ws2812_color_t ws2812_effect_pixel(const ws2812_effect_config_t *effect, uint32_t count,
                                  uint32_t index, uint64_t elapsed_us)
{
    uint32_t level = 255;
    uint64_t period = (uint64_t)effect->period_ms * 1000;
    uint64_t phase = period ? elapsed_us % period : 0;
    switch (effect->effect) {
    case WS2812_EFFECT_OFF: level = 0; break;
    case WS2812_EFFECT_SOLID: break;
    case WS2812_EFFECT_BREATH: {
        // Triangle wave with quadratic brightness response, no float math.
        uint32_t ramp = (uint32_t)((phase < period/2 ? phase : period-phase) * 510 / period);
        level = (ramp*ramp + 127) / 255;
        break;
    }
    case WS2812_EFFECT_CHASE: {
        uint32_t head = (uint32_t)(phase * count / period);
        if (effect->reverse) head = count - 1 - head;
        uint32_t distance = effect->reverse ? (index+count-head)%count : (head+count-index)%count;
        uint32_t tail = effect->tail_length ? effect->tail_length : 1;
        level = distance < tail ? (tail-distance)*255/tail : 0;
        break;
    }
    case WS2812_EFFECT_BLINK: level = phase < period/2 ? 255 : 0; break;
    }
    // Round only once, after combining color, brightness and animation level.
    uint32_t scale = effect->brightness * level;
    return (ws2812_color_t){
        .red=(effect->color.red * scale + 32512) / 65025,
        .green=(effect->color.green * scale + 32512) / 65025,
        .blue=(effect->color.blue * scale + 32512) / 65025,
    };
}
