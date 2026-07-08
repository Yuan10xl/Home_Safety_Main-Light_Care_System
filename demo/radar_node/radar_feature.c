#include "radar_feature.h"

#include <string.h>

#define RADAR_NODE_STATIC_RISK_MS 3000u

static uint8_t radar_feature_clamp_u8(int value)
{
    if (value <= 0) {
        return 0u;
    }
    if (value >= 255) {
        return 255u;
    }
    return (uint8_t)value;
}

static uint16_t radar_feature_seconds_to_ms(int seconds)
{
    uint32_t ms;

    if (seconds <= 0) {
        return 0u;
    }

    ms = (uint32_t)seconds * 1000u;
    if (ms > 0xFFFFu) {
        return 0xFFFFu;
    }

    return (uint16_t)ms;
}

void radar_feature_from_values(int has_person,
                               int motion_level,
                               int static_time_seconds,
                               int suspect_fall_hint,
                               uint32_t motion_sample_seq,
                               uint8_t fall_feature_mask,
                               uint8_t conv_feature_mask,
                               uint8_t sensor_feature_flags,
                               int radar_online,
                               radar_feature_t *feature)
{
    if (feature == NULL) {
        return;
    }

    feature->human_present = (has_person != 0) ? 1u : 0u;
    feature->motion_level = radar_feature_clamp_u8(motion_level);
    feature->static_duration_ms = radar_feature_seconds_to_ms(static_time_seconds);
    feature->fall_hint = (suspect_fall_hint != 0) ? 1u : 0u;
    feature->fall_feature_mask = fall_feature_mask;
    feature->conv_feature_mask = conv_feature_mask;
    feature->sensor_feature_flags = sensor_feature_flags;
    feature->motion_sample_seq = motion_sample_seq;
    feature->radar_online = (radar_online != 0) ? 1u : 0u;
}

void radar_feature_build_packet(const radar_feature_t *feature,
                                uint8_t node_id,
                                uint16_t seq,
                                uint32_t timestamp_ms,
                                radar_light_packet_t *packet)
{
    uint8_t conv_feature = 0u;

    if ((feature == NULL) || (packet == NULL)) {
        return;
    }

    memset(packet, 0, sizeof(*packet));
    packet->version = RADAR_LIGHT_PACKET_VERSION;
    packet->node_id = node_id;
    packet->seq = seq;
    packet->timestamp_ms = timestamp_ms;

    packet->human_present = feature->human_present;
    packet->motion_level = feature->motion_level;
    packet->static_duration_ms = feature->static_duration_ms;

    /*
     * This is a feature hint from the radar node. The final suspected-fall
     * event is decided by the gateway.
     */
    if (feature->fall_hint != 0u) {
        packet->fall_feature_1 = feature->fall_feature_mask;
        if (packet->fall_feature_1 == 0u) {
            packet->fall_feature_1 = RADAR_FALL_FEATURE_COMPOSITE;
        }
        packet->fall_feature_2 = 1u;
    }
    packet->fall_feature_value = feature->static_duration_ms;

    if (feature->motion_level <= 10u) {
        conv_feature |= RADAR_CONV_FEATURE_LOW_BAND;
    } else if (feature->motion_level > 20u) {
        conv_feature |= RADAR_CONV_FEATURE_HIGH_BAND;
    }
    conv_feature |= feature->conv_feature_mask;
    if ((feature->fall_hint != 0u) ||
        (feature->static_duration_ms >= RADAR_NODE_STATIC_RISK_MS)) {
        conv_feature |= RADAR_CONV_FEATURE_RISK_CONTEXT;
    }
    packet->convulsion_feature_1 = conv_feature;
    packet->convulsion_feature_2 = feature->motion_level;
    packet->convulsion_feature_value = feature->static_duration_ms;

    packet->radar_status =
        (feature->radar_online != 0u) ? RADAR_STATUS_ONLINE : RADAR_STATUS_OFFLINE;
    packet->reserved[RADAR_PACKET_RESERVED_SAMPLE_SEQ_LO] =
        (uint8_t)(feature->motion_sample_seq & 0xFFu);
    packet->reserved[RADAR_PACKET_RESERVED_SENSOR_FLAGS] = feature->sensor_feature_flags;
    packet->reserved[RADAR_PACKET_RESERVED_FALL_FLAGS] = packet->fall_feature_1;
    packet->reserved[RADAR_PACKET_RESERVED_CONV_FLAGS] = packet->convulsion_feature_1;
}
