#include "console_input.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>

esp_err_t console_parse_number(const char *text, double min, double max, double *out)
{
    if (!text || !*text || !out || isspace((unsigned char)*text)) return ESP_ERR_INVALID_ARG;
    errno = 0; char *end;
    double value = strtod(text, &end);
    if (errno || *end || !isfinite(value) || value < min || value > max) return ESP_ERR_INVALID_ARG;
    *out = value;
    return ESP_OK;
}

static int nibble(char c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}

esp_err_t console_parse_hex(const char *text, uint8_t *out, size_t capacity, size_t *length)
{
    if (!text || !out || !length) return ESP_ERR_INVALID_ARG;
    size_t size = 0;
    while (*text) {
        while (isspace((unsigned char)*text)) ++text;
        if (!*text) break;
        int high = nibble(*text++);
        if (high < 0 || !*text) return ESP_ERR_INVALID_ARG;
        int low = nibble(*text++);
        if (low < 0 || size == capacity || (*text && !isspace((unsigned char)*text))) return ESP_ERR_INVALID_ARG;
        out[size++] = (uint8_t)((high<<4)|low);
    }
    *length = size;
    return ESP_OK;
}
