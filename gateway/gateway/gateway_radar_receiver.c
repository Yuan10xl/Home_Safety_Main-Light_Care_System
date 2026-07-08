#include "gateway_radar_receiver.h"

#include <stdint.h>
#include <stdio.h>

#include "care_event.h"
#include "convulsion_detect_algo.h"
#include "fall_detect_algo.h"
#include "gateway_app_interface.h"
#include "gateway_sle_rx.h"

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

#define GATEWAY_SAFE_LIGHT_MIN_HOLD_MS 30000U
#define GATEWAY_SAFE_LIGHT_STABLE_MS   15000U

static uint8_t g_gateway_fall_active;
static uint8_t g_gateway_convulsion_active;
static uint8_t g_gateway_radar_was_online;
static uint8_t g_gateway_safe_light_active;
static uint32_t g_gateway_safe_light_on_ms;
static uint32_t g_gateway_safe_light_last_risk_ms;

static uint32_t gateway_elapsed_ms(uint32_t now_ms, uint32_t start_ms)
{
    return now_ms - start_ms;
}

static void gateway_safe_light_update(uint32_t now_ms, uint8_t fall_active, uint8_t convulsion_active)
{
    uint8_t risk_active = ((fall_active != 0u) || (convulsion_active != 0u)) ? 1u : 0u;

    if (risk_active != 0u) {
        g_gateway_safe_light_last_risk_ms = now_ms;
        if (g_gateway_safe_light_active == 0u) {
            int ret = gateway_sle_send_safe_lighting_cmd(GATEWAY_SAFE_LIGHT_CMD_ON,
                (fall_active != 0u) ? "suspected_fall" : "abnormal_motion_wave");
            if (ret == 0) {
                g_gateway_safe_light_active = 1u;
                g_gateway_safe_light_on_ms = now_ms;
                GATEWAY_LOG("[GATEWAY_LIGHT] safe_lighting=ON hold_ms=%u stable_ms=%u\r\n",
                            (unsigned int)GATEWAY_SAFE_LIGHT_MIN_HOLD_MS,
                            (unsigned int)GATEWAY_SAFE_LIGHT_STABLE_MS);
            }
        }
        return;
    }

    if (g_gateway_safe_light_active == 0u) {
        return;
    }

    if ((gateway_elapsed_ms(now_ms, g_gateway_safe_light_on_ms) >= GATEWAY_SAFE_LIGHT_MIN_HOLD_MS) &&
        (gateway_elapsed_ms(now_ms, g_gateway_safe_light_last_risk_ms) >= GATEWAY_SAFE_LIGHT_STABLE_MS)) {
        (void)gateway_sle_send_safe_lighting_cmd(GATEWAY_SAFE_LIGHT_CMD_OFF, "stable_recovered");
        g_gateway_safe_light_active = 0u;
        GATEWAY_LOG("[GATEWAY_LIGHT] safe_lighting=OFF reason=stable_recovered\r\n");
    }
}

void gateway_radar_receiver_init(void)
{
    fall_detect_init();
    convulsion_detect_init();
    gateway_sle_register_packet_callback(gateway_on_radar_packet);
    g_gateway_fall_active = 0u;
    g_gateway_convulsion_active = 0u;
    g_gateway_radar_was_online = 1u;
    g_gateway_safe_light_active = 0u;
    g_gateway_safe_light_on_ms = 0u;
    g_gateway_safe_light_last_risk_ms = 0u;
}

void gateway_on_radar_packet(const radar_light_packet_t *packet)
{
    fall_result_t fall_result;
    convulsion_result_t convulsion_result;
    gateway_cloud_result_t cloud_result;
    gateway_cloud_status_t cloud_status;
    uint8_t radar_online;

    if (packet == NULL) {
        return;
    }

    radar_online = ((packet->radar_status & RADAR_STATUS_OFFLINE) == 0u) ? 1u : 0u;
    if ((radar_online == 0u) && (g_gateway_radar_was_online != 0u)) {
        care_event_emit(CARE_EVENT_RADAR_OFFLINE);
    } else if ((radar_online != 0u) && (g_gateway_radar_was_online == 0u)) {
        care_event_emit(CARE_EVENT_RADAR_RECOVERED);
    }
    g_gateway_radar_was_online = radar_online;

    GATEWAY_LOG("[GATEWAY] packet seq=%u motion=%u static=%u fall_feature=0x%02x "
                "conv_feature=0x%02x new_sample=%u radar_wave=%u low_posture=%u "
                "fall_candidate=%u sample_seq_lo=%u sensor_flags=0x%02x\r\n",
                (unsigned int)packet->seq,
                (unsigned int)packet->motion_level,
                (unsigned int)packet->static_duration_ms,
                (unsigned int)packet->fall_feature_1,
                (unsigned int)packet->convulsion_feature_1,
                ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_NEW_SAMPLE) != 0u) ? 1u : 0u,
                ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_LOCAL_WAVE_HINT) != 0u) ? 1u : 0u,
                ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_LOW_POSTURE) != 0u) ? 1u : 0u,
                ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_FALL_CANDIDATE) != 0u) ? 1u : 0u,
                (unsigned int)packet->reserved[RADAR_PACKET_RESERVED_SAMPLE_SEQ_LO],
                (unsigned int)packet->reserved[RADAR_PACKET_RESERVED_SENSOR_FLAGS]);

    fall_result = fall_detect_update(packet);
    convulsion_result = convulsion_detect_update(packet);
    cloud_result = gateway_cloud_select_result(&fall_result, &convulsion_result);
    cloud_status.motion_level = packet->motion_level;
    cloud_status.static_time = packet->static_duration_ms;
    cloud_status.fall_result = fall_result.is_suspected_fall;
    cloud_status.convulsion_result = convulsion_result.is_suspected_convulsion;
    cloud_status.low_observe = fall_result.is_low_activity_observe;
    cloud_status.radar_online = radar_online;
    cloud_status.final_result = cloud_result;

    GATEWAY_LOG("[GATEWAY] fall_result=%u low_observe=%u reason=%s\r\n",
                (unsigned int)fall_result.is_suspected_fall,
                (unsigned int)fall_result.is_low_activity_observe,
                fall_result.reason);
    GATEWAY_LOG("[GATEWAY] convulsion_result=%u amp=%u cross=%u peak=%u strong=%u "
                "high_ratio=%u max_high_run=%u reason=%s\r\n",
                (unsigned int)convulsion_result.is_suspected_convulsion,
                (unsigned int)convulsion_result.amplitude,
                (unsigned int)convulsion_result.cross_count,
                (unsigned int)convulsion_result.peak_count,
                (unsigned int)convulsion_result.strong_count,
                (unsigned int)convulsion_result.high_ratio,
                (unsigned int)convulsion_result.max_high_run,
                convulsion_result.reason);

    if ((fall_result.is_suspected_fall != 0u) && (g_gateway_fall_active == 0u)) {
        g_gateway_fall_active = 1u;
        care_event_emit(CARE_EVENT_SUSPECTED_FALL);
        gateway_cloud_upload_alarm(&cloud_status,
                                   GATEWAY_CLOUD_RESULT_SUSPECTED_FALL);
    } else if (fall_result.is_suspected_fall == 0u) {
        g_gateway_fall_active = 0u;
    }

    if ((convulsion_result.is_suspected_convulsion != 0u) &&
        (g_gateway_convulsion_active == 0u)) {
        g_gateway_convulsion_active = 1u;
        care_event_emit(CARE_EVENT_SUSPECTED_CONVULSION);
        gateway_cloud_upload_alarm(
            &cloud_status,
            GATEWAY_CLOUD_RESULT_SUSPECTED_ABNORMAL_ACTIVITY_WAVE);
    } else if (convulsion_result.is_suspected_convulsion == 0u) {
        g_gateway_convulsion_active = 0u;
    }

    gateway_safe_light_update(packet->timestamp_ms,
                              fall_result.is_suspected_fall,
                              convulsion_result.is_suspected_convulsion);

    gateway_cloud_upload_result(&cloud_status);

    (void)gateway_light_on_event;
    (void)gateway_cloud_upload_event;
    (void)gateway_app_notify_event;
}
