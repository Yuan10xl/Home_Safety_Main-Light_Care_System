#ifndef RADAR_PROTOCOL_H
#define RADAR_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define RADAR_LIGHT_PACKET_VERSION 1u
#define RADAR_LIGHT_PACKET_WIRE_SIZE 27u
#define RADAR_LIGHT_PACKET_CRC_SIZE 2u
#define RADAR_LIGHT_PACKET_PAYLOAD_SIZE \
    (RADAR_LIGHT_PACKET_WIRE_SIZE - RADAR_LIGHT_PACKET_CRC_SIZE)

typedef enum {
    RADAR_PACKET_OK = 0,
    RADAR_PACKET_ERROR_NULL,
    RADAR_PACKET_ERROR_SHORT_BUFFER,
    RADAR_PACKET_ERROR_VERSION,
    RADAR_PACKET_ERROR_CRC,
} radar_packet_status_t;

uint16_t radar_protocol_crc16_ccitt(const uint8_t *data, size_t len);
const char *radar_packet_status_name(radar_packet_status_t status);

#endif /* RADAR_PROTOCOL_H */
