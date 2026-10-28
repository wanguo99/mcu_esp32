#include "runtime.h"
#include "board.h"
#include "board_definition.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
static unsigned calls;
static bool fail;
esp_err_t settings_init(void) { return ESP_OK; }
const board_config_t *board_get_config(void) { return &board_definition; }
static esp_err_t first(void) { calls=calls*10+1; return ESP_OK; }
static esp_err_t second(void) { calls=calls*10+2; return fail ? ESP_FAIL : ESP_OK; }
static esp_err_t third(void) { calls=calls*10+3; return ESP_OK; }
static esp_err_t h4(void) { return ESP_OK; }
static esp_err_t h5(void) { return ESP_OK; }
static esp_err_t h6(void) { return ESP_OK; }
static esp_err_t h7(void) { return ESP_OK; }
static esp_err_t h8(void) { return ESP_OK; }
static esp_err_t h9(void) { return ESP_OK; }
int main(void) {
    assert(runtime_start()==ESP_OK); assert(runtime_prepare_restart()==ESP_OK);
    assert(runtime_register_shutdown_hook(NULL)==ESP_ERR_INVALID_ARG);
    assert(runtime_register_shutdown_hook(first)==ESP_OK);
    assert(runtime_register_shutdown_hook(first)==ESP_OK);
    assert(runtime_register_shutdown_hook(second)==ESP_OK);
    assert(runtime_register_shutdown_hook(third)==ESP_OK);
    fail=true; assert(runtime_prepare_restart()==ESP_FAIL && calls==12);
    fail=false; calls=0; assert(runtime_prepare_restart()==ESP_OK && calls==123);
    runtime_shutdown_hook_t rest[]={h4,h5,h6,h7,h8};
    for(unsigned i=0;i<5;i++) assert(runtime_register_shutdown_hook(rest[i])==ESP_OK);
    assert(runtime_register_shutdown_hook(h9)==ESP_ERR_NO_MEM);
    assert(runtime_register_shutdown_hook(first)==ESP_OK);
}
