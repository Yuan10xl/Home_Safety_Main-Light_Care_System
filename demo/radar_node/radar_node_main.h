#ifndef RADAR_NODE_MAIN_H
#define RADAR_NODE_MAIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int has_person;
    int motion_level;
    int static_time_seconds;
    int suspect_fall_hint;
    uint32_t motion_sample_seq;
    uint8_t fall_feature_mask;
    uint8_t conv_feature_mask;
    uint8_t sensor_feature_flags;
    int radar_online;
} radar_node_sensor_snapshot_t;

void radar_node_init(uint8_t node_id);
int radar_node_send_snapshot(const radar_node_sensor_snapshot_t *snapshot,
                             uint32_t timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_NODE_MAIN_H */
