#include "peripheral_resources.h"
#include "board.h"
#include <assert.h>

static board_config_t board={.name="ESP32 resource test",.flash_mb=4};
const board_config_t *board_get_config(void) { return &board; }
static peripheral_claim_t claim(int gpio) {
    return (peripheral_claim_t){.gpios=UINT64_C(1)<<gpio,.ledc_timer=-1,.ledc_channel=-1};
}
int main(void) {
    const int reserved[]={1,3,6,7,8,9,10,11};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(*reserved);i++) {
        peripheral_claim_t c=claim(reserved[i]);
        assert(peripheral_resources_acquire(&c)==ESP_ERR_INVALID_ARG);
    }
    const int available[]={4,13,14,16,17,18,19,21,22,23,25,26,27,32,33};
    for(unsigned i=0;i<sizeof(available)/sizeof(*available);i++) {
        peripheral_claim_t c=claim(available[i]);
        assert(peripheral_resources_acquire(&c)==ESP_OK);
        peripheral_resources_release(&c);
    }
    board.psram_mb=4;
    peripheral_claim_t c=claim(16);
    assert(peripheral_resources_acquire(&c)==ESP_ERR_INVALID_ARG);
    c=claim(17); assert(peripheral_resources_acquire(&c)==ESP_ERR_INVALID_ARG);
    board.psram_mb=0;
    c=claim(13); c.ledc_timer=1; c.ledc_channel=1; c.frequency_hz=50; c.resolution_bits=14;
    assert(peripheral_resources_acquire(&c)==ESP_OK);
    assert(peripheral_resources_acquire(&c)==ESP_ERR_INVALID_STATE);
    peripheral_claim_t second=c; second.gpios=UINT64_C(1)<<14;
    assert(peripheral_resources_acquire(&second)==ESP_ERR_INVALID_STATE);
    second.ledc_channel=2;
    assert(peripheral_resources_acquire(&second)==ESP_OK);
    peripheral_claim_t conflict=c; conflict.gpios=UINT64_C(1)<<25; conflict.ledc_channel=3; conflict.frequency_hz=1000;
    assert(peripheral_resources_acquire(&conflict)==ESP_ERR_INVALID_STATE);
    peripheral_resources_release(&second);
    peripheral_resources_release(&c);
    assert(peripheral_resources_acquire(&c)==ESP_OK);
    peripheral_resources_release(&c);
}
