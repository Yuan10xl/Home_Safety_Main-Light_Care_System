#ifndef RADAR_FEATURE_H
#define RADAR_FEATURE_H

#include <stdint.h>

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t human_present;
    uint8_t motion_level;
    uint16_t static_duration_ms;
    uint8_t fall_hint;
    uint8_t fall_feature_mask;
    uint8_t conv_feature_mask;
    uint8_t sensor_feature_flags;
    uint32_t motion_sample_seq;
    uint8_t radar_online;
} radar_feature_t;

void radar_feature_from_values(int has_person,
                               int motion_level,
                               int static_time_seconds,
                               int suspect_fall_hint,
                               uint32_t motion_sample_seq,
                               uint8_t fall_feature_mask,
                               uint8_t conv_feature_mask,
                               uint8_t sensor_feature_flags,
                               int radar_online,
                               radar_feature_t *feature);
void radar_feature_build_packet(const radar_feature_t *feature,
                                uint8_t node_id,
                                uint16_t seq,
                                uint32_t timestamp_ms,
                                radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_FEATURE_H */
