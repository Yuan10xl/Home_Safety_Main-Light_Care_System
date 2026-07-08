#include "convulsion_detect_algo.h"

#include <stdbool.h>
#include <string.h>

#define CONV_LOW_ACTIVITY_THRESHOLD 10u
#define CONV_ACTIVE_THRESHOLD 20u
#define CONV_STRONG_THRESHOLD 40u
#define CONV_WINDOW_TICKS 12u
#define CONV_WATCH_TICKS 40u
#define CONV_WARMUP_TICKS 3u
#define CONV_ALERT_COOLDOWN_TICKS 20u
#define CONV_RADAR_HINT_CONFIRM_COUNT 2u
#define CONV_GATE_STATIC_MS 3000u
#define CONV_CONTINUOUS_ACTIVITY_TICKS 10u
#define CONV_AMPLITUDE_THRESHOLD 15u
#define CONV_MIN_CROSS_COUNT 4u
#define CONV_MIN_PEAK_COUNT 2u
#define CONV_MIN_STRONG_COUNT 1u
#define CONV_HIGH_RATIO_LIMIT 80u

static uint8_t g_conv_window[CONV_WINDOW_TICKS];
static uint8_t g_conv_count;
static uint8_t g_conv_write_index;
static uint8_t g_conv_watch_ticks;
static uint8_t g_conv_warmup_ticks;
static uint8_t g_conv_cooldown_ticks;
static uint8_t g_conv_radar_hint_count;

static void convulsion_reset_window(void)
{
    memset(g_conv_window, 0, sizeof(g_conv_window));
    g_conv_count = 0u;
    g_conv_write_index = 0u;
}

void convulsion_detect_init(void)
{
    convulsion_reset_window();
    g_conv_watch_ticks = 0u;
    g_conv_warmup_ticks = 0u;
    g_conv_cooldown_ticks = 0u;
    g_conv_radar_hint_count = 0u;
}

static uint8_t convulsion_get_sample(uint8_t index)
{
    if (g_conv_count < CONV_WINDOW_TICKS) {
        return g_conv_window[index];
    }

    return g_conv_window[(uint8_t)((g_conv_write_index + index) % CONV_WINDOW_TICKS)];
}

static void convulsion_push(uint8_t motion_level)
{
    if (g_conv_count < CONV_WINDOW_TICKS) {
        g_conv_window[g_conv_count++] = motion_level;
        return;
    }

    g_conv_window[g_conv_write_index] = motion_level;
    g_conv_write_index = (uint8_t)((g_conv_write_index + 1u) % CONV_WINDOW_TICKS);
}

static int8_t convulsion_band(uint8_t value)
{
    if (value <= CONV_LOW_ACTIVITY_THRESHOLD) {
        return -1;
    }

    if (value > CONV_ACTIVE_THRESHOLD) {
        return 1;
    }

    return 0;
}

static bool convulsion_packet_has_risk_context(const radar_light_packet_t *packet)
{
    if ((packet->convulsion_feature_1 &
         (RADAR_CONV_FEATURE_RISK_CONTEXT |
          RADAR_CONV_FEATURE_LOCAL_WAVE_HINT |
          RADAR_CONV_FEATURE_LOW_POSTURE |
          RADAR_CONV_FEATURE_FALL_CANDIDATE)) != 0u) {
        return true;
    }

    if ((packet->fall_feature_1 &
         (RADAR_FALL_FEATURE_OFFICIAL_HINT |
          RADAR_FALL_FEATURE_LOCAL_HINT |
          RADAR_FALL_FEATURE_COMPOSITE)) != 0u) {
        return true;
    }

    return packet->static_duration_ms >= CONV_GATE_STATIC_MS;
}

static void convulsion_start_watch_if_needed(const radar_light_packet_t *packet,
                                             convulsion_result_t *result)
{
    if (!convulsion_packet_has_risk_context(packet) || (g_conv_watch_ticks > 0u)) {
        return;
    }

    g_conv_watch_ticks = CONV_WATCH_TICKS;
    g_conv_warmup_ticks = CONV_WARMUP_TICKS;
    convulsion_reset_window();
    result->risk_context = 1u;
    result->reason = "watch_start";
}

static void convulsion_finish_tick(bool sample_tick)
{
    if (g_conv_cooldown_ticks > 0u) {
        g_conv_cooldown_ticks--;
    }

    if (sample_tick && (g_conv_watch_ticks > 0u)) {
        g_conv_watch_ticks--;
        if (g_conv_watch_ticks == 0u) {
            convulsion_reset_window();
            g_conv_radar_hint_count = 0u;
        }
    }
}

convulsion_result_t convulsion_detect_update(const radar_light_packet_t *packet)
{
    convulsion_result_t result = {0};
    uint8_t max_val = 0u;
    uint8_t min_val = 100u;
    uint8_t high_count = 0u;
    uint8_t strong_count = 0u;
    uint8_t cross_count = 0u;
    uint8_t peak_count = 0u;
    uint8_t high_run = 0u;
    uint8_t max_high_run = 0u;
    int8_t last_band = 0;
    bool new_sample;
    bool radar_wave_hint;

    result.reason = "none";

    if (packet == NULL) {
        result.reason = "null_packet";
        return result;
    }

    if ((packet->radar_status & RADAR_STATUS_OFFLINE) != 0u) {
        convulsion_detect_init();
        result.reason = "radar_offline";
        return result;
    }

    new_sample = ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_NEW_SAMPLE) != 0u);
    radar_wave_hint = ((packet->convulsion_feature_1 & RADAR_CONV_FEATURE_LOCAL_WAVE_HINT) != 0u);

    convulsion_start_watch_if_needed(packet, &result);
    result.risk_context = (g_conv_watch_ticks > 0u) ? 1u : result.risk_context;

    if (radar_wave_hint && (result.risk_context != 0u)) {
        if (g_conv_radar_hint_count < CONV_RADAR_HINT_CONFIRM_COUNT) {
            g_conv_radar_hint_count++;
        }
    } else if (!radar_wave_hint) {
        g_conv_radar_hint_count = 0u;
    }

    if (g_conv_radar_hint_count >= CONV_RADAR_HINT_CONFIRM_COUNT) {
        result.wave_feature = 1u;
        if (g_conv_cooldown_ticks == 0u) {
            result.is_suspected_convulsion = 1u;
            result.reason = "radar_motion_wave_hint_confirmed";
            g_conv_cooldown_ticks = CONV_ALERT_COOLDOWN_TICKS;
        } else {
            result.reason = "radar_motion_wave_hint_cooldown";
        }
        convulsion_finish_tick(new_sample);
        return result;
    }

    if (!new_sample) {
        result.reason = radar_wave_hint ? "duplicate_radar_wave_hint" : "duplicate_sample";
        convulsion_finish_tick(false);
        return result;
    }

    convulsion_push(packet->motion_level);

    if (g_conv_warmup_ticks > 0u) {
        g_conv_warmup_ticks--;
        result.reason = "warmup";
        convulsion_finish_tick(true);
        return result;
    }

    if (g_conv_count < CONV_WINDOW_TICKS) {
        result.reason = "collecting_window";
        convulsion_finish_tick(true);
        return result;
    }

    for (uint8_t i = 0u; i < CONV_WINDOW_TICKS; i++) {
        uint8_t value = convulsion_get_sample(i);
        int8_t band = convulsion_band(value);

        if (value > max_val) {
            max_val = value;
        }
        if (value < min_val) {
            min_val = value;
        }
        if (value > CONV_ACTIVE_THRESHOLD) {
            high_count++;
            high_run++;
            if (high_run > max_high_run) {
                max_high_run = high_run;
            }
        } else {
            high_run = 0u;
        }
        if (value > CONV_STRONG_THRESHOLD) {
            strong_count++;
        }
        if (band != 0) {
            if ((last_band != 0) && (band != last_band)) {
                cross_count++;
            }
            last_band = band;
        }
    }

    for (uint8_t i = 1u; i + 1u < CONV_WINDOW_TICKS; i++) {
        uint8_t prev = convulsion_get_sample((uint8_t)(i - 1u));
        uint8_t curr = convulsion_get_sample(i);
        uint8_t next = convulsion_get_sample((uint8_t)(i + 1u));

        if ((curr > CONV_ACTIVE_THRESHOLD) && (curr > prev) && (curr >= next)) {
            peak_count++;
        }
    }

    result.amplitude = (uint16_t)(max_val - min_val);
    result.cross_count = cross_count;
    result.peak_count = peak_count;
    result.strong_count = strong_count;
    result.high_ratio = (uint8_t)((high_count * 100u) / CONV_WINDOW_TICKS);
    result.max_high_run = max_high_run;
    result.normal_continuous_activity =
        ((result.high_ratio > CONV_HIGH_RATIO_LIMIT) ||
         (max_high_run > CONV_CONTINUOUS_ACTIVITY_TICKS)) ? 1u : 0u;
    result.wave_feature =
        ((result.amplitude >= CONV_AMPLITUDE_THRESHOLD) &&
         (cross_count >= CONV_MIN_CROSS_COUNT) &&
         (peak_count >= CONV_MIN_PEAK_COUNT) &&
         (strong_count >= CONV_MIN_STRONG_COUNT) &&
         (result.normal_continuous_activity == 0u)) ? 1u : 0u;

    if (result.normal_continuous_activity != 0u) {
        result.reason = "continuous_activity";
        convulsion_finish_tick(true);
        return result;
    }

    if (result.wave_feature == 0u) {
        result.reason = "wave_feature_not_met";
        convulsion_finish_tick(true);
        return result;
    }

    if (result.risk_context == 0u) {
        result.reason = "no_risk_context";
        convulsion_finish_tick(true);
        return result;
    }

    if (g_conv_cooldown_ticks == 0u) {
        result.is_suspected_convulsion = 1u;
        result.reason = "abnormal_motion_wave";
        g_conv_cooldown_ticks = CONV_ALERT_COOLDOWN_TICKS;
    } else {
        result.reason = "cooldown";
    }

    convulsion_finish_tick(true);
    return result;
}
