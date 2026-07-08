#include "radar_protocol.h"

uint16_t radar_protocol_crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFFu;

    if (data == NULL) {
        return crc;
    }

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }

    return crc;
}

const char *radar_packet_status_name(radar_packet_status_t status)
{
    switch (status) {
        case RADAR_PACKET_OK:
            return "ok";
        case RADAR_PACKET_ERROR_NULL:
            return "null";
        case RADAR_PACKET_ERROR_SHORT_BUFFER:
            return "short_buffer";
        case RADAR_PACKET_ERROR_VERSION:
            return "version";
        case RADAR_PACKET_ERROR_CRC:
            return "crc";
        default:
            return "unknown";
    }
}
