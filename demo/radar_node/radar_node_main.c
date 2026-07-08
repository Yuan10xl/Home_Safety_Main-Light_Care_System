#include "radar_node_main.h"

#include "radar_feature.h"
#include "radar_sle_tx.h"
#include "soc_osal.h"

#ifndef RADAR_NODE_LOG
#define RADAR_NODE_LOG(...) osal_printk(__VA_ARGS__)
#endif

static uint8_t g_radar_node_id = 1u;
static uint16_t g_radar_node_seq;

void radar_node_init(uint8_t node_id)
{
    g_radar_node_id = node_id;
    g_radar_node_seq = 0u;
    (void)radar_sle_tx_init();
}

int radar_node_send_snapshot(const radar_node_sensor_snapshot_t *snapshot,
                             uint32_t timestamp_ms)
{
    radar_feature_t feature;
    radar_light_packet_t packet;

    if (snapshot == NULL) {
        return -1;
    }

    radar_feature_from_values(snapshot->has_person,
                              snapshot->motion_level,
                              snapshot->static_time_seconds,
                              snapshot->suspect_fall_hint,
                              snapshot->motion_sample_seq,
                              snapshot->fall_feature_mask,
                              snapshot->conv_feature_mask,
                              snapshot->sensor_feature_flags,
                              snapshot->radar_online,
                              &feature);
    radar_feature_build_packet(&feature,
                               g_radar_node_id,
                               g_radar_node_seq++,
                               timestamp_ms,
                               &packet);

    RADAR_NODE_LOG("[RADAR_NODE] feature: present=%u motion=%u static=%u "
                   "fall_hint=%u fall_feature=0x%02x conv_feature=0x%02x "
                   "sensor_flags=0x%02x sample_seq=%u new_sample=%u status=%u\r\n",
                   (unsigned int)feature.human_present,
                   (unsigned int)feature.motion_level,
                   (unsigned int)feature.static_duration_ms,
                   (unsigned int)feature.fall_hint,
                   (unsigned int)packet.fall_feature_1,
                   (unsigned int)packet.convulsion_feature_1,
                   (unsigned int)feature.sensor_feature_flags,
                   (unsigned int)feature.motion_sample_seq,
                   ((packet.convulsion_feature_1 & RADAR_CONV_FEATURE_NEW_SAMPLE) != 0u) ? 1u : 0u,
                   (unsigned int)packet.radar_status);

    return radar_sle_send_packet(&packet);
}
