#ifndef RADAR_PACKET_H
#define RADAR_PACKET_H

#include "radar_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADAR_FALL_FEATURE_OFFICIAL_HINT 0x01u
#define RADAR_FALL_FEATURE_LOCAL_HINT    0x02u
#define RADAR_FALL_FEATURE_COMPOSITE     0x04u

#define RADAR_CONV_FEATURE_RISK_CONTEXT       0x01u
#define RADAR_CONV_FEATURE_LOW_BAND           0x02u
#define RADAR_CONV_FEATURE_HIGH_BAND          0x04u
#define RADAR_CONV_FEATURE_LOCAL_WAVE_HINT    0x08u
#define RADAR_CONV_FEATURE_LOW_POSTURE        0x10u
#define RADAR_CONV_FEATURE_FALL_CANDIDATE     0x20u
#define RADAR_CONV_FEATURE_NEW_SAMPLE         0x80u

#define RADAR_PACKET_RESERVED_SAMPLE_SEQ_LO   0u
#define RADAR_PACKET_RESERVED_SENSOR_FLAGS    1u
#define RADAR_PACKET_RESERVED_FALL_FLAGS      2u
#define RADAR_PACKET_RESERVED_CONV_FLAGS      3u

#define RADAR_STATUS_ONLINE              0x00u
#define RADAR_STATUS_OFFLINE             0x01u

typedef struct {
    uint8_t version;
    uint8_t node_id;
    uint16_t seq;
    uint32_t timestamp_ms;

    uint8_t human_present;
    uint8_t motion_level;
    uint16_t static_duration_ms;

    uint8_t fall_feature_1;
    uint8_t fall_feature_2;
    uint16_t fall_feature_value;

    uint8_t convulsion_feature_1;
    uint8_t convulsion_feature_2;
    uint16_t convulsion_feature_value;

    uint8_t radar_status;
    uint8_t reserved[4];

    uint16_t crc16;
} radar_light_packet_t;

radar_packet_status_t radar_packet_encode(const radar_light_packet_t *packet,
                                           uint8_t *out,
                                           size_t out_len,
                                           size_t *encoded_len);
radar_packet_status_t radar_packet_decode(const uint8_t *data,
                                           size_t data_len,
                                           radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_PACKET_H */
