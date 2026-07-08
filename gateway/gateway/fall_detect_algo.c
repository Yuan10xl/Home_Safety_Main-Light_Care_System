#include "fall_detect_algo.h"

#include <stdbool.h>

#define FALL_LOW_ACTIVITY_THRESHOLD 10u
#define FALL_ACTIVE_THRESHOLD 20u
#define FALL_ACTIVE_CONFIRM_COUNT 2u
#define FALL_LOW_ACTIVITY_OBSERVE_MS 10000u
/* Gateway-side confirmation for radar-board fall hints.
 * The radar board is still the source of posture/fall features, but the gateway
 * filters isolated hints that appear while standing still. A valid fall hint
 * needs recent obvious activity and two consecutive packets before it is raised.
 */
#define FALL_FEATURE_CONFIRM_COUNT 2u
#define FALL_RECENT_ACTIVITY_HOLD_TICKS 60u
#define FALL_ALERT_HOLD_TICKS 20u

static uint8_t g_fall_active_count;
static uint8_t g_fall_feature_count;
static uint8_t g_fall_recent_activity_ticks;
static uint8_t g_fall_alert_hold_ticks;
static bool g_fall_active_seen;

void fall_detect_init(void)
{
    g_fall_active_count = 0u;
    g_fall_feature_count = 0u;
    g_fall_recent_activity_ticks = 0u;
    g_fall_alert_hold_ticks = 0u;
    g_fall_active_seen = false;
}

fall_result_t fall_detect_update(const radar_light_packet_t *packet)
{
    fall_result_t result = {0u, 0u, "none"};
    bool fall_feature;

    if (packet == NULL) {
        result.reason = "null_packet";
        return result;
    }

    if ((packet->radar_status & RADAR_STATUS_OFFLINE) != 0u) {
        fall_detect_init();
        result.reason = "radar_offline";
        return result;
    }

    if (packet->motion_level > FALL_ACTIVE_THRESHOLD) {
        if (g_fall_active_count < FALL_ACTIVE_CONFIRM_COUNT) {
            g_fall_active_count++;
        }
        if (g_fall_active_count >= FALL_ACTIVE_CONFIRM_COUNT) {
            g_fall_active_seen = true;
        }
        g_fall_recent_activity_ticks = FALL_RECENT_ACTIVITY_HOLD_TICKS;
    } else {
        g_fall_active_count = 0u;
        if (g_fall_recent_activity_ticks > 0u) {
            g_fall_recent_activity_ticks--;
        }
    }

    fall_feature =
        (packet->fall_feature_1 &
         (RADAR_FALL_FEATURE_OFFICIAL_HINT |
          RADAR_FALL_FEATURE_LOCAL_HINT |
          RADAR_FALL_FEATURE_COMPOSITE)) != 0u;

    if (fall_feature && (g_fall_recent_activity_ticks > 0u)) {
        if (g_fall_feature_count < FALL_FEATURE_CONFIRM_COUNT) {
            g_fall_feature_count++;
        }
    } else {
        g_fall_feature_count = 0u;
    }

    if (g_fall_feature_count >= FALL_FEATURE_CONFIRM_COUNT) {
        g_fall_alert_hold_ticks = FALL_ALERT_HOLD_TICKS;
    }

    if (g_fall_alert_hold_ticks > 0u) {
        g_fall_alert_hold_ticks--;
        result.is_suspected_fall = 1u;
        result.reason = "radar_fall_feature_confirmed";
        return result;
    }

    if (fall_feature) {
        result.reason = (g_fall_recent_activity_ticks > 0u) ?
                        "fall_feature_confirming" :
                        "fall_feature_no_recent_activity";
        return result;
    }

    if (g_fall_active_seen &&
        (packet->motion_level <= FALL_LOW_ACTIVITY_THRESHOLD) &&
        (packet->static_duration_ms >= FALL_LOW_ACTIVITY_OBSERVE_MS)) {
        result.is_low_activity_observe = 1u;
        result.reason = "low_activity_after_activity";
        return result;
    }

    return result;
}
