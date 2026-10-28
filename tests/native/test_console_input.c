#include "console_input.h"
#include <assert.h>
static void input(void) {
    double result=42; const char *bad[]={""," 1","1x","nan","inf","1e999","-1","101"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        assert(console_parse_number(bad[i],0,100,&result)==ESP_ERR_INVALID_ARG); assert(result==42);
    }
    assert(console_parse_number("12.5",0,100,&result)==ESP_OK && result==12.5);
    uint8_t bytes[2]; size_t length=77;
    assert(console_parse_hex("A0 01",bytes,2,&length)==ESP_OK && length==2 && bytes[0]==0xa0);
    assert(console_parse_hex("A0 01 FF",bytes,2,&length)!=ESP_OK && length==2);
    assert(console_parse_hex("A 01",bytes,2,&length)!=ESP_OK);
    assert(console_parse_hex("GG",bytes,2,&length)!=ESP_OK);
}
int main(void) { input(); }
