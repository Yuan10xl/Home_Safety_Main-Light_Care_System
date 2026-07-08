#include "radar_packet.h"

#include <string.h>

static void radar_packet_write_u16(uint8_t *data, size_t *offset, uint16_t value)
{
    data[(*offset)++] = (uint8_t)(value & 0xFFu);
    data[(*offset)++] = (uint8_t)((value >> 8) & 0xFFu);
}

static void radar_packet_write_u32(uint8_t *data, size_t *offset, uint32_t value)
{
    data[(*offset)++] = (uint8_t)(value & 0xFFu);
    data[(*offset)++] = (uint8_t)((value >> 8) & 0xFFu);
    data[(*offset)++] = (uint8_t)((value >> 16) & 0xFFu);
    data[(*offset)++] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint16_t radar_packet_read_u16(const uint8_t *data, size_t *offset)
{
    uint16_t value = (uint16_t)data[*offset] |
                     ((uint16_t)data[*offset + 1u] << 8);
    *offset += 2u;
    return value;
}

static uint32_t radar_packet_read_u32(const uint8_t *data, size_t *offset)
{
    uint32_t value = (uint32_t)data[*offset] |
                     ((uint32_t)data[*offset + 1u] << 8) |
                     ((uint32_t)data[*offset + 2u] << 16) |
                     ((uint32_t)data[*offset + 3u] << 24);
    *offset += 4u;
    return value;
}

radar_packet_status_t radar_packet_encode(const radar_light_packet_t *packet,
                                           uint8_t *out,
                                           size_t out_len,
                                           size_t *encoded_len)
{
    size_t offset = 0u;
    uint16_t crc;

    if ((packet == NULL) || (out == NULL)) {
        return RADAR_PACKET_ERROR_NULL;
    }

    if (out_len < RADAR_LIGHT_PACKET_WIRE_SIZE) {
        return RADAR_PACKET_ERROR_SHORT_BUFFER;
    }

    out[offset++] = RADAR_LIGHT_PACKET_VERSION;
    out[offset++] = packet->node_id;
    radar_packet_write_u16(out, &offset, packet->seq);
    radar_packet_write_u32(out, &offset, packet->timestamp_ms);

    out[offset++] = packet->human_present;
    out[offset++] = packet->motion_level;
    radar_packet_write_u16(out, &offset, packet->static_duration_ms);

    out[offset++] = packet->fall_feature_1;
    out[offset++] = packet->fall_feature_2;
    radar_packet_write_u16(out, &offset, packet->fall_feature_value);

    out[offset++] = packet->convulsion_feature_1;
    out[offset++] = packet->convulsion_feature_2;
    radar_packet_write_u16(out, &offset, packet->convulsion_feature_value);

    out[offset++] = packet->radar_status;
    memcpy(&out[offset], packet->reserved, sizeof(packet->reserved));
    offset += sizeof(packet->reserved);

    crc = radar_protocol_crc16_ccitt(out, RADAR_LIGHT_PACKET_PAYLOAD_SIZE);
    radar_packet_write_u16(out, &offset, crc);

    if (encoded_len != NULL) {
        *encoded_len = offset;
    }

    return RADAR_PACKET_OK;
}

radar_packet_status_t radar_packet_decode(const uint8_t *data,
                                           size_t data_len,
                                           radar_light_packet_t *packet)
{
    size_t offset = 0u;
    uint16_t expected_crc;
    uint16_t packet_crc;

    if ((data == NULL) || (packet == NULL)) {
        return RADAR_PACKET_ERROR_NULL;
    }

    if (data_len < RADAR_LIGHT_PACKET_WIRE_SIZE) {
        return RADAR_PACKET_ERROR_SHORT_BUFFER;
    }

    expected_crc = radar_protocol_crc16_ccitt(data, RADAR_LIGHT_PACKET_PAYLOAD_SIZE);
    packet_crc = (uint16_t)data[RADAR_LIGHT_PACKET_PAYLOAD_SIZE] |
                 ((uint16_t)data[RADAR_LIGHT_PACKET_PAYLOAD_SIZE + 1u] << 8);
    if (expected_crc != packet_crc) {
        return RADAR_PACKET_ERROR_CRC;
    }

    packet->version = data[offset++];
    if (packet->version != RADAR_LIGHT_PACKET_VERSION) {
        return RADAR_PACKET_ERROR_VERSION;
    }

    packet->node_id = data[offset++];
    packet->seq = radar_packet_read_u16(data, &offset);
    packet->timestamp_ms = radar_packet_read_u32(data, &offset);

    packet->human_present = data[offset++];
    packet->motion_level = data[offset++];
    packet->static_duration_ms = radar_packet_read_u16(data, &offset);

    packet->fall_feature_1 = data[offset++];
    packet->fall_feature_2 = data[offset++];
    packet->fall_feature_value = radar_packet_read_u16(data, &offset);

    packet->convulsion_feature_1 = data[offset++];
    packet->convulsion_feature_2 = data[offset++];
    packet->convulsion_feature_value = radar_packet_read_u16(data, &offset);

    packet->radar_status = data[offset++];
    memcpy(packet->reserved, &data[offset], sizeof(packet->reserved));
    offset += sizeof(packet->reserved);

    packet->crc16 = packet_crc;
    return RADAR_PACKET_OK;
}
