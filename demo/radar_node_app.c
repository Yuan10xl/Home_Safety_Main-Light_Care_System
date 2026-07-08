/*
 * Radar-board application entry.
 *
 * This file turns the standalone SEN0623 radar driver into the radar node
 * described by the final two-board design: collect radar_result_t locally,
 * build the lightweight radar packet, and hand it to the SLE transmit wrapper.
 * The gateway board owns final event decisions and light/cloud/app actions.
 */

#include "radar_sensor.h"
#include "radar_lamp_switch.h"

#include <stdbool.h>

#include "app_init.h"
#include "common_def.h"
#include "radar_node/radar_node_main.h"
#include "common/radar_protocol/radar_packet.h"
#include "soc_osal.h"

#define RADAR_NODE_APP_STACK_SIZE      0x1000
#define RADAR_NODE_APP_PRIO            25
#define RADAR_NODE_ID                  1
#define RADAR_NODE_SEND_PERIOD_MS      500


static uint32_t g_radar_node_timestamp_ms;
static uint32_t g_radar_node_last_motion_sample_seq;
static bool g_radar_node_has_motion_sample_seq;

static int radar_node_app_task(const char *arg)
{
    unused(arg);

    osal_printk("[RADAR_NODE] app start\r\n");
    radar_sensor_start();
    radar_node_init(RADAR_NODE_ID);
    /*
     * Start lamp GPIO control after the SLE stack has had time to finish its
     * early pin setup. GPIO11 is also touched by the SDK SIO/I2S pinmux path,
     * so initializing the switch input last keeps manual control stable.
     */
    osal_msleep(1000);
    (void)radar_lamp_switch_start();

    while (1) {
        const radar_result_t *result = radar_sensor_get_result();
        radar_sensor_feature_state_t feature_state;
        radar_node_sensor_snapshot_t snapshot;
        uint8_t fall_feature_mask = 0;
        uint8_t conv_feature_mask = 0;

        radar_sensor_get_feature_state(&feature_state);
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_FALL_OFFICIAL) != 0) {
            fall_feature_mask |= RADAR_FALL_FEATURE_OFFICIAL_HINT;
        }
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_FALL_LOCAL) != 0) {
            fall_feature_mask |= RADAR_FALL_FEATURE_LOCAL_HINT;
        }
        if (result->suspect_fall_hint != 0) {
            fall_feature_mask |= RADAR_FALL_FEATURE_COMPOSITE;
        }
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_MOTION_WAVE_HINT) != 0) {
            conv_feature_mask |= RADAR_CONV_FEATURE_LOCAL_WAVE_HINT;
        }
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_MOTION_WAVE_RISK) != 0) {
            conv_feature_mask |= RADAR_CONV_FEATURE_RISK_CONTEXT;
        }
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_LOW_POSTURE_CONTEXT) != 0) {
            conv_feature_mask |= RADAR_CONV_FEATURE_LOW_POSTURE;
        }
        if ((feature_state.feature_flags & RADAR_SENSOR_FEATURE_FALL_CANDIDATE_RECENT) != 0) {
            conv_feature_mask |= RADAR_CONV_FEATURE_FALL_CANDIDATE;
        }
        if ((!g_radar_node_has_motion_sample_seq) ||
            (feature_state.motion_sample_seq != g_radar_node_last_motion_sample_seq)) {
            conv_feature_mask |= RADAR_CONV_FEATURE_NEW_SAMPLE;
            g_radar_node_last_motion_sample_seq = feature_state.motion_sample_seq;
            g_radar_node_has_motion_sample_seq = true;
        }

        snapshot.has_person = result->has_person;
        snapshot.motion_level = result->motion_level;
        snapshot.static_time_seconds = result->static_time;
        snapshot.suspect_fall_hint = result->suspect_fall_hint;
        snapshot.motion_sample_seq = feature_state.motion_sample_seq;
        snapshot.fall_feature_mask = fall_feature_mask;
        snapshot.conv_feature_mask = conv_feature_mask;
        snapshot.sensor_feature_flags = feature_state.feature_flags;
        /*
         * Online means the radar-board task and driver are alive. It must not
         * be tied to has_person: an empty room is still a healthy radar node.
         */
        snapshot.radar_online = 1;

        (void)radar_node_send_snapshot(&snapshot, g_radar_node_timestamp_ms);
        g_radar_node_timestamp_ms += RADAR_NODE_SEND_PERIOD_MS;

        osal_msleep(RADAR_NODE_SEND_PERIOD_MS);
    }

    return 0;
}

static void radar_node_app_start(void)
{
    osal_task *task = NULL;

    osal_kthread_lock();
    task = osal_kthread_create((osal_kthread_handler)radar_node_app_task, NULL,
                               "RadarNodeApp", RADAR_NODE_APP_STACK_SIZE);
    if (task != NULL) {
        (void)osal_kthread_set_priority(task, RADAR_NODE_APP_PRIO);
        osal_printk("[RADAR_NODE] task created\r\n");
    } else {
        osal_printk("[RADAR_NODE] task create failed\r\n");
    }
    osal_kthread_unlock();
}

app_run(radar_node_app_start);
