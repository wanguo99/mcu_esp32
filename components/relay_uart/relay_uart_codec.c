#include "relay_uart_codec.h"
#include <string.h>

static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
    }
    return crc;
}

void relay_modbus_request(uint8_t address, uint16_t coil, bool read, bool on, uint8_t out[8])
{
    out[0] = address;
    out[1] = read ? 0x01 : 0x05;
    out[2] = coil >> 8;
    out[3] = coil & 0xff;
    out[4] = read ? 0 : (on ? 0xff : 0);
    out[5] = read ? 1 : 0;
    uint16_t crc = crc16(out, 6);
    out[6] = crc & 0xff;
    out[7] = crc >> 8;
}

esp_err_t relay_modbus_response(const uint8_t request[8], const uint8_t *response,
                                size_t length, bool *on)
{
    if (!request || !response || !on || length < 5) return ESP_ERR_INVALID_ARG;
    uint16_t crc = crc16(response, length - 2);
    if (response[length-2] != (crc & 0xff) || response[length-1] != (crc >> 8))
        return ESP_ERR_INVALID_CRC;
    if (response[0] != request[0] || response[1] != request[1]) return ESP_ERR_INVALID_RESPONSE;
    if (request[1] == 0x05) {
        if (length != 8 || memcmp(request, response, 8)) return ESP_ERR_INVALID_RESPONSE;
        *on = request[4] == 0xff;
    } else if (request[1] == 0x01) {
        if (length != 6 || response[2] != 1) return ESP_ERR_INVALID_RESPONSE;
        *on = (response[3] & 1) != 0;
    } else return ESP_ERR_NOT_SUPPORTED;
    return ESP_OK;
}
