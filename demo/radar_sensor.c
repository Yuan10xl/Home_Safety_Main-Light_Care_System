#include "radar_sensor.h"

#include <stdbool.h>

#include "common_def.h"
#include "errcode.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"

#define RADAR_SENSOR_UART_BUS            UART_BUS_1
#define RADAR_SENSOR_UART_TX_PIN         GPIO_15
#define RADAR_SENSOR_UART_RX_PIN         GPIO_16
#define RADAR_SENSOR_UART_PIN_MODE       PIN_MODE_1
#define RADAR_SENSOR_UART_BAUD_RATE      115200

#define RADAR_SENSOR_RX_BUFFER_SIZE      128
#define RADAR_SENSOR_READ_BUFFER_SIZE    64
#define RADAR_SENSOR_FRAME_MIN_SIZE      9
#define RADAR_SENSOR_FRAME_MAX_DATA_SIZE 16
#define RADAR_SENSOR_FRAME_CACHE_SIZE    128
#define RADAR_SENSOR_RESPONSE_WAIT_MS    80
#define RADAR_SENSOR_QUERY_PERIOD_MS     500
#define RADAR_SENSOR_TICKS_PER_SECOND    (1000 / RADAR_SENSOR_QUERY_PERIOD_MS)

#define RADAR_SENSOR_ACTIVITY_CTRL       0x80
#define RADAR_SENSOR_FALL_CTRL           0x83
#define RADAR_SENSOR_MODE_CTRL           0x02
#define RADAR_SENSOR_ACTIVITY_QUERY_CMD  0x83
#define RADAR_SENSOR_ACTIVITY_REPORT_CMD 0x03
#define RADAR_SENSOR_FALL_STATE_CMD      0x81
#define RADAR_SENSOR_FALL_BREAK_HEIGHT_CMD 0x91
#define RADAR_SENSOR_STATIC_RESIDENCY_STATE_CMD 0x85
#define RADAR_SENSOR_STATIC_RESIDENCY_TIME_CMD 0x8A
#define RADAR_SENSOR_FALL_SENSITIVITY_CMD 0x8D
#define RADAR_SENSOR_HEIGHT_RATIO_SWITCH_CMD 0x95
#define RADAR_SENSOR_FALL_TIME_CMD       0x8C
#define RADAR_SENSOR_HEIGHT_DURATION_CMD 0x8F
#define RADAR_SENSOR_SEATED_HDIST_CMD    0x8D
#define RADAR_SENSOR_MOTION_HDIST_CMD    0x8E
#define RADAR_SENSOR_TRACK_CMD           0x8E
#define RADAR_SENSOR_MODE_QUERY_CMD      0xA8
#define RADAR_SENSOR_MODE_SET_CMD        0x08
#define RADAR_SENSOR_MODE_FALLING        0x01
#define RADAR_SENSOR_MODE_SLEEP          0x02
#define RADAR_SENSOR_FALL_QUERY_INTERVAL_TICKS 2
#define RADAR_SENSOR_FALL_DEBUG_QUERY_INTERVAL_TICKS 2
#define RADAR_SENSOR_FALL_DEBUG_QUERY_COUNT 10
#define RADAR_SENSOR_MODE_QUERY_RETRY    3
#define RADAR_SENSOR_MODE_RESPONSE_WAIT_MS 120
#define RADAR_SENSOR_MODE_RETRY_WAIT_MS  80
#define RADAR_SENSOR_MODE_SWITCH_WAIT_MS 10000
#define RADAR_SENSOR_MODE_RECHECK_INTERVAL_TICKS \
    (60 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_NO_VALID_LOG_COUNT  5
#define RADAR_SENSOR_OFFLINE_COUNT       20
#define RADAR_SENSOR_LOW_POSTURE_HOLD_SECONDS 10
#define RADAR_SENSOR_LOW_POSTURE_TIMEOUT_SECONDS 12
#define RADAR_SENSOR_HORIZONTAL_MOVE_THRESHOLD 80
#define RADAR_SENSOR_FALL_RULE_LOG_INTERVAL_TICKS \
    (2 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_FALL_ALERT_HOLD_TICKS \
    (10 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_FALL_SOURCE_OFFICIAL 0x01
#define RADAR_SENSOR_FALL_SOURCE_RULE     0x02
#define RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS \
    (10 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_MOTION_WAVE_WATCH_TICKS \
    (15 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_MOTION_WAVE_WARMUP_TICKS 6
#define RADAR_SENSOR_MOTION_WAVE_ALERT_COOLDOWN_TICKS \
    (10 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_MOTION_WAVE_GATE_SECONDS       3
#define RADAR_SENSOR_MOTION_WAVE_LOW_POSITION_SECONDS 3
#define RADAR_SENSOR_MOTION_WAVE_CONTINUOUS_ACTIVITY_TICKS \
    (4 * RADAR_SENSOR_TICKS_PER_SECOND)
#define RADAR_SENSOR_MOTION_WAVE_AMPLITUDE          20
#define RADAR_SENSOR_MOTION_WAVE_MIN_CROSS_COUNT    6
#define RADAR_SENSOR_MOTION_WAVE_MIN_PEAK_COUNT     3
#define RADAR_SENSOR_MOTION_WAVE_MIN_STRONG_COUNT   2
#define RADAR_SENSOR_MOTION_WAVE_HIGH_RATIO_NUM     3
#define RADAR_SENSOR_MOTION_WAVE_HIGH_RATIO_DEN     4

#define RADAR_SENSOR_ACTIVE_CONFIRM_COUNT         2
#define RADAR_SENSOR_LOW_ACTIVITY_CONFIRM_COUNT   \
    (RADAR_SENSOR_ABNORMAL_LOW_ACTIVITY_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND)

/*
 * Threshold notes:
 * Static/no-person background is mostly motion_level 1-2.
 * A still person or light disturbance is mostly 1-2 and can occasionally
 * reach 6-9. The first-version logic therefore treats <=10 as low activity,
 * >20 as obvious activity, and keeps >40 as a strong-activity reference.
 */

#define RADAR_SENSOR_TASK_STACK_SIZE     0x1000
#define RADAR_SENSOR_TASK_PRIO           24

static uint8_t g_radar_sensor_uart_rx_buffer[RADAR_SENSOR_RX_BUFFER_SIZE];
static uint8_t g_radar_sensor_frame_cache[RADAR_SENSOR_FRAME_CACHE_SIZE];
static uint32_t g_radar_sensor_frame_cache_len;
static uint32_t g_radar_sensor_no_valid_count;
static uint32_t g_radar_sensor_active_count;
static uint32_t g_radar_sensor_static_ticks;
static int g_radar_sensor_last_static_time_log = -1;
static bool g_radar_sensor_active_seen;
static bool g_radar_sensor_abnormal_low_logged;
static bool g_radar_sensor_fall_logged;
static bool g_radar_sensor_started;
static bool g_radar_sensor_official_fall_hint;
static bool g_radar_sensor_rule_fall_hint;
static uint32_t g_radar_sensor_fall_alert_hold_ticks;
static uint8_t g_radar_sensor_fall_alert_source_mask;
static uint8_t g_radar_sensor_motion_wave_window[RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS];
static uint32_t g_radar_sensor_motion_wave_count;
static uint32_t g_radar_sensor_motion_wave_write_index;
static bool g_radar_sensor_motion_wave_logged;
static bool g_radar_sensor_motion_wave_debug_logged;
static bool g_radar_sensor_motion_wave_pending;
static uint8_t g_radar_sensor_motion_wave_pending_level;
static bool g_radar_sensor_motion_wave_fall_candidate_recent;
static uint32_t g_radar_sensor_motion_wave_watch_ticks;
static uint32_t g_radar_sensor_motion_wave_warmup_ticks;
static uint32_t g_radar_sensor_motion_wave_alert_cooldown_ticks;
static uint32_t g_radar_sensor_motion_wave_hint_ticks;
static uint32_t g_radar_sensor_motion_sample_seq;
static radar_result_t g_radar_sensor_result = {
    .area_id = AREA_NORMAL,
};

typedef enum {
    RADAR_FALL_RULE_NORMAL = 0,
    RADAR_FALL_RULE_LOW_HOLD,
    RADAR_FALL_RULE_SENSOR_CONFIRMED,
} radar_fall_rule_state_t;

typedef struct {
    uint8_t ctrl;
    uint8_t cmd;
    uint16_t data_len;
    uint8_t data[RADAR_SENSOR_FRAME_MAX_DATA_SIZE];
} radar_sensor_frame_t;

static radar_fall_rule_state_t g_radar_sensor_fall_rule_state = RADAR_FALL_RULE_NORMAL;
static uint32_t g_radar_sensor_low_posture_ticks;
static uint32_t g_radar_sensor_low_posture_age_ticks =
    RADAR_SENSOR_LOW_POSTURE_TIMEOUT_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND + 1;
static uint32_t g_radar_sensor_last_low_posture_log_tick;
static bool g_radar_sensor_fall_rule_candidate_logged;
static bool g_radar_sensor_track_valid;
static bool g_radar_sensor_track_baseline_valid;
static bool g_radar_sensor_hdist_valid;
static bool g_radar_sensor_hdist_baseline_valid;
static uint16_t g_radar_sensor_track_x;
static uint16_t g_radar_sensor_track_y;
static uint16_t g_radar_sensor_track_baseline_x;
static uint16_t g_radar_sensor_track_baseline_y;
static uint16_t g_radar_sensor_seated_hdist;
static uint16_t g_radar_sensor_motion_hdist;
static uint16_t g_radar_sensor_hdist_baseline;

static uart_buffer_config_t g_radar_sensor_uart_buffer_config = {
    .rx_buffer = g_radar_sensor_uart_rx_buffer,
    .rx_buffer_size = RADAR_SENSOR_RX_BUFFER_SIZE,
};

static const uint8_t g_radar_sensor_activity_query[] = {
    0x53, 0x59, 0x80, 0x83, 0x00, 0x01, 0x0F, 0xBF, 0x54, 0x43
};

static const uint8_t g_radar_sensor_fall_query[] = {
    0x53, 0x59, 0x83, 0x81, 0x00, 0x01, 0x0F, 0xC0, 0x54, 0x43
};

static const uint8_t g_radar_sensor_fall_break_height_query[] = {
    0x53, 0x59, 0x83, 0x91, 0x00, 0x01, 0x0F, 0xD0, 0x54, 0x43
};

static const uint8_t g_radar_sensor_static_residency_state_query[] = {
    0x53, 0x59, 0x83, 0x85, 0x00, 0x01, 0x0F, 0xC4, 0x54, 0x43
};

static const uint8_t g_radar_sensor_static_residency_time_query[] = {
    0x53, 0x59, 0x83, 0x8A, 0x00, 0x01, 0x0F, 0xC9, 0x54, 0x43
};

static const uint8_t g_radar_sensor_fall_sensitivity_query[] = {
    0x53, 0x59, 0x83, 0x8D, 0x00, 0x01, 0x0F, 0xCC, 0x54, 0x43
};

static const uint8_t g_radar_sensor_height_ratio_switch_query[] = {
    0x53, 0x59, 0x83, 0x95, 0x00, 0x01, 0x0F, 0xD4, 0x54, 0x43
};

static const uint8_t g_radar_sensor_fall_time_query[] = {
    0x53, 0x59, 0x83, 0x8C, 0x00, 0x01, 0x0F, 0xCB, 0x54, 0x43
};

static const uint8_t g_radar_sensor_height_duration_query[] = {
    0x53, 0x59, 0x83, 0x8F, 0x00, 0x01, 0x0F, 0xCE, 0x54, 0x43
};

static const uint8_t g_radar_sensor_seated_hdist_query[] = {
    0x53, 0x59, 0x80, 0x8D, 0x00, 0x01, 0x0F, 0xC9, 0x54, 0x43
};

static const uint8_t g_radar_sensor_motion_hdist_query[] = {
    0x53, 0x59, 0x80, 0x8E, 0x00, 0x01, 0x0F, 0xCA, 0x54, 0x43
};

static const uint8_t g_radar_sensor_track_query[] = {
    0x53, 0x59, 0x83, 0x8E, 0x00, 0x01, 0x0F, 0xCD, 0x54, 0x43
};

static const uint8_t g_radar_sensor_mode_query[] = {
    0x53, 0x59, 0x02, 0xA8, 0x00, 0x01, 0x0F, 0x66, 0x54, 0x43
};

static const uint8_t g_radar_sensor_set_falling_mode[] = {
    0x53, 0x59, 0x02, 0x08, 0x00, 0x01, 0x01, 0xB8, 0x54, 0x43
};

const radar_result_t *radar_sensor_get_result(void)
{
    return &g_radar_sensor_result;
}

void radar_sensor_get_feature_state(radar_sensor_feature_state_t *state)
{
    uint8_t flags = 0;

    if (state == NULL) {
        return;
    }

    if ((g_radar_sensor_result.suspect_fall_hint != 0) &&
        ((g_radar_sensor_fall_alert_source_mask & RADAR_SENSOR_FALL_SOURCE_OFFICIAL) != 0)) {
        flags |= RADAR_SENSOR_FEATURE_FALL_OFFICIAL;
    }
    if ((g_radar_sensor_result.suspect_fall_hint != 0) &&
        ((g_radar_sensor_fall_alert_source_mask & RADAR_SENSOR_FALL_SOURCE_RULE) != 0)) {
        flags |= RADAR_SENSOR_FEATURE_FALL_LOCAL;
    }
    if (g_radar_sensor_motion_wave_hint_ticks > 0) {
        flags |= RADAR_SENSOR_FEATURE_MOTION_WAVE_HINT;
    }
    if (g_radar_sensor_motion_wave_watch_ticks > 0) {
        flags |= RADAR_SENSOR_FEATURE_MOTION_WAVE_RISK;
    }
    if ((g_radar_sensor_fall_rule_state == RADAR_FALL_RULE_LOW_HOLD) &&
        (g_radar_sensor_low_posture_ticks >=
         RADAR_SENSOR_MOTION_WAVE_LOW_POSITION_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND)) {
        flags |= RADAR_SENSOR_FEATURE_LOW_POSTURE_CONTEXT;
    }
    if (g_radar_sensor_motion_wave_fall_candidate_recent) {
        flags |= RADAR_SENSOR_FEATURE_FALL_CANDIDATE_RECENT;
    }

    state->motion_sample_seq = g_radar_sensor_motion_sample_seq;
    state->feature_flags = flags;
}

static const char *radar_sensor_work_mode_name(uint8_t mode)
{
    if (mode == RADAR_SENSOR_MODE_FALLING) {
        return "falling";
    }
    if (mode == RADAR_SENSOR_MODE_SLEEP) {
        return "sleep";
    }
    return "unknown";
}

static uint8_t radar_sensor_checksum(const uint8_t *frame, uint32_t len_without_checksum)
{
    uint16_t sum = 0;

    for (uint32_t i = 0; i < len_without_checksum; i++) {
        sum += frame[i];
    }
    return (uint8_t)(sum & 0xFF);
}

static uint16_t radar_sensor_read_u16_be(const uint8_t *data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static uint32_t radar_sensor_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static uint16_t radar_sensor_abs_diff_u16(uint16_t a, uint16_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

static uint16_t radar_sensor_current_hdist(void)
{
    return (g_radar_sensor_motion_hdist != 0) ? g_radar_sensor_motion_hdist :
           g_radar_sensor_seated_hdist;
}

static uint16_t radar_sensor_horizontal_delta(void)
{
    uint16_t delta = 0;

    if (g_radar_sensor_track_valid && g_radar_sensor_track_baseline_valid) {
        uint16_t dx = radar_sensor_abs_diff_u16(g_radar_sensor_track_x,
                                                g_radar_sensor_track_baseline_x);
        uint16_t dy = radar_sensor_abs_diff_u16(g_radar_sensor_track_y,
                                                g_radar_sensor_track_baseline_y);
        delta = (dx > dy) ? dx : dy;
    }

    if (g_radar_sensor_hdist_valid && g_radar_sensor_hdist_baseline_valid) {
        uint16_t hdist_delta = radar_sensor_abs_diff_u16(radar_sensor_current_hdist(),
                                                        g_radar_sensor_hdist_baseline);
        if (hdist_delta > delta) {
            delta = hdist_delta;
        }
    }

    return delta;
}

static void radar_sensor_motion_wave_reset_window(void)
{
    g_radar_sensor_motion_wave_count = 0;
    g_radar_sensor_motion_wave_write_index = 0;
    g_radar_sensor_motion_wave_logged = false;
    g_radar_sensor_motion_wave_debug_logged = false;
    g_radar_sensor_motion_wave_pending = false;
}

static void radar_sensor_motion_wave_clear_risk_context(void)
{
    g_radar_sensor_motion_wave_fall_candidate_recent = false;
    g_radar_sensor_motion_wave_watch_ticks = 0;
    g_radar_sensor_motion_wave_warmup_ticks = 0;
    radar_sensor_motion_wave_reset_window();
}

static bool radar_sensor_motion_wave_low_position_context(void)
{
    return (g_radar_sensor_fall_rule_state == RADAR_FALL_RULE_LOW_HOLD) &&
           (g_radar_sensor_low_posture_ticks >=
            RADAR_SENSOR_MOTION_WAVE_LOW_POSITION_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND);
}

static const char *radar_sensor_motion_wave_watch_reason(void)
{
    if (g_radar_sensor_result.suspect_fall_hint != 0) {
        return "fall_hint";
    }
    if (g_radar_sensor_motion_wave_fall_candidate_recent) {
        return "fall_candidate";
    }
    if (radar_sensor_motion_wave_low_position_context()) {
        return "low_position";
    }
    if (g_radar_sensor_result.static_time >= RADAR_SENSOR_MOTION_WAVE_GATE_SECONDS) {
        return "static_time";
    }

    return NULL;
}

static void radar_sensor_motion_wave_start_watch_if_needed(void)
{
    const char *reason = radar_sensor_motion_wave_watch_reason();

    if ((reason == NULL) || (g_radar_sensor_motion_wave_watch_ticks > 0)) {
        return;
    }

    g_radar_sensor_motion_wave_watch_ticks = RADAR_SENSOR_MOTION_WAVE_WATCH_TICKS;
    g_radar_sensor_motion_wave_warmup_ticks = RADAR_SENSOR_MOTION_WAVE_WARMUP_TICKS;
    radar_sensor_motion_wave_reset_window();
    osal_printk("[RADAR_WAVE] watch_start reason=%s reset_window=1\r\n", reason);
}

static void radar_sensor_motion_wave_finish_tick(void)
{
    if (g_radar_sensor_motion_wave_alert_cooldown_ticks > 0) {
        g_radar_sensor_motion_wave_alert_cooldown_ticks--;
    }
    if (g_radar_sensor_motion_wave_hint_ticks > 0) {
        g_radar_sensor_motion_wave_hint_ticks--;
    }

    if (g_radar_sensor_motion_wave_watch_ticks == 0) {
        return;
    }

    g_radar_sensor_motion_wave_watch_ticks--;
    if (g_radar_sensor_motion_wave_watch_ticks > 0) {
        return;
    }

    g_radar_sensor_motion_wave_fall_candidate_recent = false;
    if (radar_sensor_motion_wave_watch_reason() == NULL) {
        radar_sensor_motion_wave_reset_window();
    }
}

/*
 * suspect_fall_hint is a composite suspected-fall risk output:
 *   1. SEN0623 official 83_81 fall hint.
 *   2. Local low-posture hold + stable horizontal position after obvious activity.
 *   3. Both sources together.
 * This is only a suspected risk hint for family confirmation, not a diagnosis
 * or confirmed fall event.
 */
static bool radar_sensor_rule_fall_condition(uint16_t *horizontal_delta,
                                             uint32_t *low_hold_seconds)
{
    *horizontal_delta = radar_sensor_horizontal_delta();
    *low_hold_seconds = g_radar_sensor_low_posture_ticks / RADAR_SENSOR_TICKS_PER_SECOND;

    bool low_hold_ok =
        (g_radar_sensor_low_posture_ticks >=
         RADAR_SENSOR_LOW_POSTURE_HOLD_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND);
    bool stable_position = (*horizontal_delta <= RADAR_SENSOR_HORIZONTAL_MOVE_THRESHOLD);

    return g_radar_sensor_active_seen && low_hold_ok && stable_position;
}

static uint8_t radar_sensor_update_fall_sources(uint16_t *horizontal_delta,
                                                uint32_t *low_hold_seconds)
{
    uint8_t source_mask = 0;

    g_radar_sensor_rule_fall_hint =
        radar_sensor_rule_fall_condition(horizontal_delta, low_hold_seconds);

    if (g_radar_sensor_official_fall_hint) {
        source_mask |= RADAR_SENSOR_FALL_SOURCE_OFFICIAL;
    }
    if (g_radar_sensor_rule_fall_hint) {
        source_mask |= RADAR_SENSOR_FALL_SOURCE_RULE;
    }

    return source_mask;
}

static void radar_sensor_log_fall_alert(uint8_t source_mask, uint16_t horizontal_delta,
                                        uint32_t low_hold_seconds)
{
    if (source_mask == (RADAR_SENSOR_FALL_SOURCE_OFFICIAL | RADAR_SENSOR_FALL_SOURCE_RULE)) {
        osal_printk("[RADAR_ALERT] suspect_fall_hint source=both low_hold=%u horizontal_delta=%u "
                    "static_time=%d motion_level=%d\r\n",
                    (unsigned int)low_hold_seconds, horizontal_delta,
                    g_radar_sensor_result.static_time,
                    g_radar_sensor_result.motion_level);
        return;
    }

    if ((source_mask & RADAR_SENSOR_FALL_SOURCE_OFFICIAL) != 0) {
        osal_printk("[RADAR_ALERT] suspect_fall_hint source=official_83_81\r\n");
        return;
    }

    if ((source_mask & RADAR_SENSOR_FALL_SOURCE_RULE) != 0) {
        osal_printk("[RADAR_ALERT] suspect_fall_hint source=rule_low_posture low_hold=%u "
                    "horizontal_delta=%u static_time=%d motion_level=%d\r\n",
                    (unsigned int)low_hold_seconds, horizontal_delta,
                    g_radar_sensor_result.static_time,
                    g_radar_sensor_result.motion_level);
    }
}

static void radar_sensor_refresh_fall_hint(bool advance_hold)
{
    uint16_t horizontal_delta = 0;
    uint32_t low_hold_seconds = 0;
    uint8_t source_mask = radar_sensor_update_fall_sources(&horizontal_delta, &low_hold_seconds);

    if (source_mask != 0) {
        g_radar_sensor_fall_alert_hold_ticks = RADAR_SENSOR_FALL_ALERT_HOLD_TICKS;
        if (source_mask != g_radar_sensor_fall_alert_source_mask) {
            radar_sensor_log_fall_alert(source_mask, horizontal_delta, low_hold_seconds);
            g_radar_sensor_fall_alert_source_mask = source_mask;
        }
    } else if (advance_hold && (g_radar_sensor_fall_alert_hold_ticks > 0)) {
        g_radar_sensor_fall_alert_hold_ticks--;
    }

    if ((source_mask != 0) || (g_radar_sensor_fall_alert_hold_ticks > 0)) {
        g_radar_sensor_result.suspect_fall_hint = 1;
        if (!g_radar_sensor_fall_logged) {
            g_radar_sensor_fall_logged = true;
            osal_printk("[RADAR_SENSOR] fall_detected_hint\r\n");
        }
        return;
    }

    g_radar_sensor_result.suspect_fall_hint = 0;
    if (g_radar_sensor_fall_logged) {
        g_radar_sensor_fall_logged = false;
        g_radar_sensor_fall_alert_source_mask = 0;
        osal_printk("[RADAR_SENSOR] fall_hint_cleared\r\n");
    }
}

static void radar_sensor_clear_local_fall_rule_hint(void)
{
    g_radar_sensor_rule_fall_hint = false;
    radar_sensor_refresh_fall_hint(false);
}

static void radar_sensor_fall_rule_reset(const char *reason)
{
    if (g_radar_sensor_fall_rule_state != RADAR_FALL_RULE_NORMAL) {
        osal_printk("[RADAR_STATE] fall_rule_reset, reason=%s\r\n", reason);
    }

    g_radar_sensor_fall_rule_state = RADAR_FALL_RULE_NORMAL;
    g_radar_sensor_low_posture_ticks = 0;
    g_radar_sensor_low_posture_age_ticks =
        RADAR_SENSOR_LOW_POSTURE_TIMEOUT_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND + 1;
    g_radar_sensor_last_low_posture_log_tick = 0;
    g_radar_sensor_fall_rule_candidate_logged = false;
    g_radar_sensor_track_baseline_valid = false;
    g_radar_sensor_hdist_baseline_valid = false;
    g_radar_sensor_motion_wave_fall_candidate_recent = false;
    radar_sensor_clear_local_fall_rule_hint();
    if (radar_sensor_motion_wave_watch_reason() == NULL) {
        if (g_radar_sensor_motion_wave_watch_ticks > 0) {
            osal_printk("[RADAR_WAVE] watch_clear reason=%s\r\n", reason);
        }
        radar_sensor_motion_wave_clear_risk_context();
    }
}

static void radar_sensor_low_posture_evidence(const char *reason, uint32_t value)
{
    g_radar_sensor_low_posture_age_ticks = 0;

    if (g_radar_sensor_fall_rule_state == RADAR_FALL_RULE_NORMAL) {
        g_radar_sensor_fall_rule_state = RADAR_FALL_RULE_LOW_HOLD;
        g_radar_sensor_low_posture_ticks = 0;
        g_radar_sensor_last_low_posture_log_tick = 0;
        g_radar_sensor_fall_rule_candidate_logged = false;
        if (g_radar_sensor_track_valid) {
            g_radar_sensor_track_baseline_x = g_radar_sensor_track_x;
            g_radar_sensor_track_baseline_y = g_radar_sensor_track_y;
            g_radar_sensor_track_baseline_valid = true;
        }
        if (g_radar_sensor_hdist_valid) {
            g_radar_sensor_hdist_baseline = radar_sensor_current_hdist();
            g_radar_sensor_hdist_baseline_valid = true;
        }
        osal_printk("[RADAR_STATE] low_posture_hold_start, reason=%s, value=%u\r\n",
                    reason, (unsigned int)value);
    }
}

static void radar_sensor_fall_rule_confirm_by_sensor(void)
{
    if (g_radar_sensor_fall_rule_state != RADAR_FALL_RULE_SENSOR_CONFIRMED) {
        g_radar_sensor_fall_rule_state = RADAR_FALL_RULE_SENSOR_CONFIRMED;
        osal_printk("[RADAR_STATE] fall_rule_confirmed, source=83_81\r\n");
    }
}

static void radar_sensor_fall_rule_tick(void)
{
    if (g_radar_sensor_low_posture_age_ticks <=
        RADAR_SENSOR_LOW_POSTURE_TIMEOUT_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND) {
        g_radar_sensor_low_posture_age_ticks++;
    }

    if (g_radar_sensor_fall_rule_state != RADAR_FALL_RULE_LOW_HOLD) {
        return;
    }

    uint16_t horizontal_delta = radar_sensor_horizontal_delta();
    if (horizontal_delta > RADAR_SENSOR_HORIZONTAL_MOVE_THRESHOLD) {
        osal_printk("[RADAR_STATE] fall_rule_rejected_horizontal_move, delta=%u\r\n",
                    horizontal_delta);
        radar_sensor_fall_rule_reset("horizontal_move");
        return;
    }

    if (g_radar_sensor_low_posture_age_ticks >
        RADAR_SENSOR_LOW_POSTURE_TIMEOUT_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND) {
        osal_printk("[RADAR_STATE] fall_rule_rejected_low_posture_lost\r\n");
        radar_sensor_fall_rule_reset("low_posture_lost");
        return;
    }

    if (g_radar_sensor_low_posture_ticks < UINT32_MAX) {
        g_radar_sensor_low_posture_ticks++;
    }

    if ((g_radar_sensor_low_posture_ticks > 0) &&
        ((g_radar_sensor_low_posture_ticks - g_radar_sensor_last_low_posture_log_tick) >=
         RADAR_SENSOR_FALL_RULE_LOG_INTERVAL_TICKS)) {
        g_radar_sensor_last_low_posture_log_tick = g_radar_sensor_low_posture_ticks;
        osal_printk("[RADAR_STATE] low_posture_hold=%u, horizontal_delta=%u\r\n",
                    (unsigned int)(g_radar_sensor_low_posture_ticks / RADAR_SENSOR_TICKS_PER_SECOND),
                    horizontal_delta);
    }

    if ((g_radar_sensor_low_posture_ticks >=
         RADAR_SENSOR_LOW_POSTURE_HOLD_SECONDS * RADAR_SENSOR_TICKS_PER_SECOND) &&
        !g_radar_sensor_fall_rule_candidate_logged) {
        g_radar_sensor_fall_rule_candidate_logged = true;
        g_radar_sensor_motion_wave_fall_candidate_recent = true;
        radar_sensor_motion_wave_start_watch_if_needed();
        osal_printk("[RADAR_STATE] fall_rule_candidate, low_hold=%u, horizontal_delta=%u\r\n",
                    (unsigned int)(g_radar_sensor_low_posture_ticks / RADAR_SENSOR_TICKS_PER_SECOND),
                    horizontal_delta);
    }

    radar_sensor_refresh_fall_hint(false);
}

static void radar_sensor_drop_cache_prefix(uint32_t count)
{
    if (count >= g_radar_sensor_frame_cache_len) {
        g_radar_sensor_frame_cache_len = 0;
        return;
    }

    uint32_t remain = g_radar_sensor_frame_cache_len - count;
    for (uint32_t i = 0; i < remain; i++) {
        g_radar_sensor_frame_cache[i] = g_radar_sensor_frame_cache[count + i];
    }
    g_radar_sensor_frame_cache_len = remain;
}

static void radar_sensor_cache_append(const uint8_t *data, uint32_t len)
{
    if (len == 0) {
        return;
    }

    if (len > RADAR_SENSOR_FRAME_CACHE_SIZE) {
        data += len - RADAR_SENSOR_FRAME_CACHE_SIZE;
        len = RADAR_SENSOR_FRAME_CACHE_SIZE;
    }

    if (g_radar_sensor_frame_cache_len + len > RADAR_SENSOR_FRAME_CACHE_SIZE) {
        uint32_t overflow = g_radar_sensor_frame_cache_len + len - RADAR_SENSOR_FRAME_CACHE_SIZE;
        radar_sensor_drop_cache_prefix(overflow);
    }

    for (uint32_t i = 0; i < len; i++) {
        g_radar_sensor_frame_cache[g_radar_sensor_frame_cache_len + i] = data[i];
    }
    g_radar_sensor_frame_cache_len += len;
}

static bool radar_sensor_parse_frame(radar_sensor_frame_t *frame)
{
    while (g_radar_sensor_frame_cache_len >= RADAR_SENSOR_FRAME_MIN_SIZE) {
        uint32_t frame_start = 0;
        while ((frame_start < g_radar_sensor_frame_cache_len) &&
               (g_radar_sensor_frame_cache[frame_start] != 0x53)) {
            frame_start++;
        }
        if (frame_start > 0) {
            radar_sensor_drop_cache_prefix(frame_start);
        }
        if (g_radar_sensor_frame_cache_len < RADAR_SENSOR_FRAME_MIN_SIZE) {
            return false;
        }

        if (g_radar_sensor_frame_cache[1] != 0x59) {
            radar_sensor_drop_cache_prefix(1);
            continue;
        }

        uint16_t data_len = ((uint16_t)g_radar_sensor_frame_cache[4] << 8) | g_radar_sensor_frame_cache[5];
        if (data_len > RADAR_SENSOR_FRAME_MAX_DATA_SIZE) {
            radar_sensor_drop_cache_prefix(1);
            continue;
        }

        uint32_t total_len = 6 + data_len + 3;
        if (g_radar_sensor_frame_cache_len < total_len) {
            return false;
        }

        uint32_t checksum_index = 6 + data_len;
        if ((g_radar_sensor_frame_cache[checksum_index] ==
             radar_sensor_checksum(g_radar_sensor_frame_cache, checksum_index)) &&
            (g_radar_sensor_frame_cache[checksum_index + 1] == 0x54) &&
            (g_radar_sensor_frame_cache[checksum_index + 2] == 0x43)) {
            frame->ctrl = g_radar_sensor_frame_cache[2];
            frame->cmd = g_radar_sensor_frame_cache[3];
            frame->data_len = data_len;
            for (uint32_t i = 0; i < data_len; i++) {
                frame->data[i] = g_radar_sensor_frame_cache[6 + i];
            }
            radar_sensor_drop_cache_prefix(total_len);
            return true;
        }

        radar_sensor_drop_cache_prefix(1);
    }

    return false;
}

static void radar_sensor_set_offline_if_needed(void)
{
    g_radar_sensor_no_valid_count++;
    if ((g_radar_sensor_no_valid_count >= RADAR_SENSOR_NO_VALID_LOG_COUNT) &&
        ((g_radar_sensor_no_valid_count % RADAR_SENSOR_NO_VALID_LOG_COUNT) == 0)) {
        osal_printk("[RADAR_SENSOR] no valid frame\r\n");
    }

    if (g_radar_sensor_no_valid_count >= RADAR_SENSOR_OFFLINE_COUNT) {
        g_radar_sensor_result.has_person = 0;
        g_radar_sensor_result.motion_level = 0;
        g_radar_sensor_result.static_time = 0;
        g_radar_sensor_result.stay_time = 0;
        g_radar_sensor_result.suspect_fall_hint = 0;
        g_radar_sensor_result.area_stay_hint = 0;
        g_radar_sensor_static_ticks = 0;
        g_radar_sensor_last_static_time_log = -1;
        g_radar_sensor_active_count = 0;
        g_radar_sensor_active_seen = false;
        g_radar_sensor_abnormal_low_logged = false;
        g_radar_sensor_fall_logged = false;
        g_radar_sensor_official_fall_hint = false;
        g_radar_sensor_rule_fall_hint = false;
        g_radar_sensor_fall_alert_hold_ticks = 0;
        g_radar_sensor_fall_alert_source_mask = 0;
        g_radar_sensor_motion_wave_hint_ticks = 0;
        radar_sensor_fall_rule_reset("offline");
        radar_sensor_motion_wave_clear_risk_context();
    }
}

static uint8_t radar_sensor_motion_wave_get_sample(uint32_t index)
{
    if (g_radar_sensor_motion_wave_count < RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS) {
        return g_radar_sensor_motion_wave_window[index];
    }

    return g_radar_sensor_motion_wave_window[
        (g_radar_sensor_motion_wave_write_index + index) % RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS];
}

static void radar_sensor_motion_wave_push(uint8_t motion_level)
{
    if (g_radar_sensor_motion_wave_count < RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS) {
        g_radar_sensor_motion_wave_window[g_radar_sensor_motion_wave_count] = motion_level;
        g_radar_sensor_motion_wave_count++;
        return;
    }

    g_radar_sensor_motion_wave_window[g_radar_sensor_motion_wave_write_index] = motion_level;
    g_radar_sensor_motion_wave_write_index =
        (g_radar_sensor_motion_wave_write_index + 1) % RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS;
}

static bool radar_sensor_motion_wave_risk_context(void)
{
    bool scene_risk_context = false; /* Reserved for a future main-control scene gate. */

    return (g_radar_sensor_motion_wave_watch_ticks > 0) ||
           scene_risk_context;
}

static int8_t radar_sensor_motion_wave_band(uint8_t value)
{
    if (value <= RADAR_SENSOR_LOW_ACTIVITY_THRESHOLD) {
        return -1;
    }

    if (value > RADAR_SENSOR_ACTIVE_THRESHOLD) {
        return 1;
    }

    return 0;
}

static void radar_sensor_motion_wave_update(uint8_t motion_level)
{
    uint8_t max_val = 0;
    uint8_t min_val = 100;
    uint32_t high_count = 0;
    uint32_t strong_count = 0;
    uint32_t cross_count = 0;
    uint32_t peak_count = 0;
    uint32_t high_run = 0;
    uint32_t max_high_run = 0;
    int8_t last_band = 0;

    radar_sensor_motion_wave_start_watch_if_needed();
    radar_sensor_motion_wave_push(motion_level);

    if (g_radar_sensor_motion_wave_warmup_ticks > 0) {
        osal_printk("[RADAR_WAVE] warmup ticks=%u\r\n",
                    (unsigned int)g_radar_sensor_motion_wave_warmup_ticks);
        g_radar_sensor_motion_wave_warmup_ticks--;
        radar_sensor_motion_wave_finish_tick();
        return;
    }

    if (g_radar_sensor_motion_wave_count < RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS) {
        radar_sensor_motion_wave_finish_tick();
        return;
    }

    for (uint32_t i = 0; i < RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS; i++) {
        uint8_t value = radar_sensor_motion_wave_get_sample(i);
        int8_t band = radar_sensor_motion_wave_band(value);

        if (value > max_val) {
            max_val = value;
        }
        if (value < min_val) {
            min_val = value;
        }
        if (value > RADAR_SENSOR_ACTIVE_THRESHOLD) {
            high_count++;
            high_run++;
            if (high_run > max_high_run) {
                max_high_run = high_run;
            }
        } else {
            high_run = 0;
        }
        if (value > RADAR_SENSOR_STRONG_THRESHOLD) {
            strong_count++;
        }

        if (band != 0) {
            if ((last_band != 0) && (band != last_band)) {
                cross_count++;
            }
            last_band = band;
        }
    }

    for (uint32_t i = 1; i + 1 < RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS; i++) {
        uint8_t prev = radar_sensor_motion_wave_get_sample(i - 1);
        uint8_t curr = radar_sensor_motion_wave_get_sample(i);
        uint8_t next = radar_sensor_motion_wave_get_sample(i + 1);

        if ((curr > RADAR_SENSOR_ACTIVE_THRESHOLD) && (curr > prev) && (curr >= next)) {
            peak_count++;
        }
    }

    uint32_t amplitude = (uint32_t)(max_val - min_val);
    uint32_t high_ratio =
        (high_count * 100) / RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS;
    bool high_ratio_over_limit =
        (high_count * RADAR_SENSOR_MOTION_WAVE_HIGH_RATIO_DEN) >
        (RADAR_SENSOR_MOTION_WAVE_WINDOW_TICKS * RADAR_SENSOR_MOTION_WAVE_HIGH_RATIO_NUM);
    bool normal_continuous_activity =
        high_ratio_over_limit ||
        (max_high_run > RADAR_SENSOR_MOTION_WAVE_CONTINUOUS_ACTIVITY_TICKS);
    bool wave_feature =
        (amplitude >= RADAR_SENSOR_MOTION_WAVE_AMPLITUDE) &&
        (cross_count >= RADAR_SENSOR_MOTION_WAVE_MIN_CROSS_COUNT) &&
        (peak_count >= RADAR_SENSOR_MOTION_WAVE_MIN_PEAK_COUNT) &&
        (strong_count >= RADAR_SENSOR_MOTION_WAVE_MIN_STRONG_COUNT) &&
        !normal_continuous_activity;
    bool risk_context = radar_sensor_motion_wave_risk_context();
    bool motion_wave = risk_context && wave_feature;

    if (risk_context || wave_feature) {
        osal_printk("[RADAR_WAVE] amp=%u cross=%u peak=%u high_ratio=%u max_high_run=%u risk=%u\r\n",
                    amplitude, cross_count, peak_count, high_ratio, max_high_run,
                    risk_context ? 1 : 0);
    }

    if (normal_continuous_activity) {
        if (!risk_context) {
            g_radar_sensor_motion_wave_logged = false;
        }
        g_radar_sensor_motion_wave_debug_logged = false;
        radar_sensor_motion_wave_finish_tick();
        return;
    }

    if (!wave_feature) {
        if (!risk_context) {
            g_radar_sensor_motion_wave_logged = false;
        }
        g_radar_sensor_motion_wave_debug_logged = false;
        radar_sensor_motion_wave_finish_tick();
        return;
    }

    if (!risk_context) {
        g_radar_sensor_motion_wave_logged = false;
        if (!g_radar_sensor_motion_wave_debug_logged) {
            g_radar_sensor_motion_wave_debug_logged = true;
            osal_printk("[RADAR_DEBUG] motion_wave_detected_but_no_risk_context\r\n");
            osal_printk("[RADAR_SENSOR] motion_wave_metrics max=%u, min=%u, amplitude=%u, "
                        "high_count=%u, strong_count=%u, cross_count=%u, peak_count=%u, "
                        "max_high_run=%u\r\n",
                        max_val, min_val, amplitude, high_count, strong_count,
                        cross_count, peak_count, max_high_run);
        }
        radar_sensor_motion_wave_finish_tick();
        return;
    }

    g_radar_sensor_motion_wave_debug_logged = false;
    if (motion_wave && !g_radar_sensor_motion_wave_logged &&
        (g_radar_sensor_motion_wave_alert_cooldown_ticks == 0)) {
        g_radar_sensor_motion_wave_logged = true;
        g_radar_sensor_motion_wave_alert_cooldown_ticks =
            RADAR_SENSOR_MOTION_WAVE_ALERT_COOLDOWN_TICKS;
        g_radar_sensor_motion_wave_hint_ticks =
            RADAR_SENSOR_MOTION_WAVE_ALERT_COOLDOWN_TICKS;
        osal_printk("[RADAR_ALERT] suspect_abnormal_motion_wave\r\n");
        osal_printk("[RADAR_SENSOR] motion_wave_metrics max=%u, min=%u, amplitude=%u, "
                    "high_count=%u, strong_count=%u, cross_count=%u, peak_count=%u, "
                    "max_high_run=%u\r\n",
                    max_val, min_val, amplitude, high_count, strong_count,
                    cross_count, peak_count, max_high_run);
    }
    radar_sensor_motion_wave_finish_tick();
}

static void radar_sensor_update_fall(uint8_t fall_state)
{
    bool official_fall_hint = (fall_state != 0);
    bool previous_official_fall_hint = g_radar_sensor_official_fall_hint;

    g_radar_sensor_no_valid_count = 0;
    g_radar_sensor_official_fall_hint = official_fall_hint;

    osal_printk("[RADAR_SENSOR] official_fall_hint=%d, source=83_81\r\n",
                official_fall_hint ? 1 : 0);

    if (official_fall_hint) {
        radar_sensor_refresh_fall_hint(false);
        radar_sensor_motion_wave_start_watch_if_needed();
        radar_sensor_fall_rule_confirm_by_sensor();
    } else if (previous_official_fall_hint) {
        radar_sensor_fall_rule_reset("sensor_fall_cleared");
    } else {
        radar_sensor_refresh_fall_hint(false);
    }
}

static void radar_sensor_update_result(uint8_t motion_level, uint8_t source_cmd)
{
    g_radar_sensor_no_valid_count = 0;
    g_radar_sensor_result.has_person = 1;
    g_radar_sensor_result.motion_level = motion_level;
    g_radar_sensor_result.area_id = AREA_NORMAL;
    g_radar_sensor_result.stay_time = 0;
    g_radar_sensor_result.area_stay_hint = 0;

    g_radar_sensor_motion_sample_seq++;
    osal_printk("[RADAR_SENSOR] motion_level=%u, source=80_%02X\r\n", motion_level, source_cmd);
    g_radar_sensor_motion_wave_pending_level = motion_level;
    g_radar_sensor_motion_wave_pending = true;

    if (motion_level > RADAR_SENSOR_ACTIVE_THRESHOLD) {
        if (g_radar_sensor_active_count < RADAR_SENSOR_ACTIVE_CONFIRM_COUNT) {
            g_radar_sensor_active_count++;
        }

        if (g_radar_sensor_active_seen && (g_radar_sensor_static_ticks > 0)) {
            g_radar_sensor_static_ticks = 0;
            g_radar_sensor_result.static_time = 0;
            g_radar_sensor_last_static_time_log = -1;
            g_radar_sensor_abnormal_low_logged = false;
            osal_printk("[RADAR_SENSOR] active_recovered\r\n");
        } else if (!g_radar_sensor_active_seen &&
                   (g_radar_sensor_active_count >= RADAR_SENSOR_ACTIVE_CONFIRM_COUNT)) {
            g_radar_sensor_active_seen = true;
            osal_printk("[RADAR_SENSOR] active\r\n");
        }
        return;
    }

    g_radar_sensor_active_count = 0;
    if (!g_radar_sensor_active_seen) {
        return;
    }

    if (motion_level > RADAR_SENSOR_LOW_ACTIVITY_THRESHOLD) {
        /*
         * 11-20 is a transition band: do not count it as low activity, but
         * also do not clear static_time. Only confirmed activity >20 clears
         * the low-activity timer and prints active_recovered.
         */
        return;
    }

    if (g_radar_sensor_static_ticks < UINT32_MAX) {
        g_radar_sensor_static_ticks++;
    }
    g_radar_sensor_result.static_time = (int)(g_radar_sensor_static_ticks / RADAR_SENSOR_TICKS_PER_SECOND);
    if ((g_radar_sensor_result.static_time > 0) &&
        (g_radar_sensor_result.static_time != g_radar_sensor_last_static_time_log)) {
        g_radar_sensor_last_static_time_log = g_radar_sensor_result.static_time;
        osal_printk("[RADAR_SENSOR] static_time=%d, area_stay_hint=0\r\n",
                    g_radar_sensor_result.static_time);
    }
    radar_sensor_motion_wave_start_watch_if_needed();

    if ((g_radar_sensor_static_ticks >= RADAR_SENSOR_LOW_ACTIVITY_CONFIRM_COUNT) &&
        !g_radar_sensor_abnormal_low_logged) {
        g_radar_sensor_abnormal_low_logged = true;
        osal_printk("[RADAR_SENSOR] abnormal_low_activity, static_time=%d, area_stay_hint=0\r\n",
                    g_radar_sensor_result.static_time);
    }
}

static bool radar_sensor_handle_debug_frame(const radar_sensor_frame_t *frame)
{
    if (frame->ctrl == RADAR_SENSOR_ACTIVITY_CTRL) {
        switch (frame->cmd) {
            case RADAR_SENSOR_SEATED_HDIST_CMD:
                if (frame->data_len >= 2) {
                    g_radar_sensor_seated_hdist = radar_sensor_read_u16_be(frame->data);
                    g_radar_sensor_hdist_valid = true;
                    osal_printk("[RADAR_SENSOR] seated_horizontal_distance=%u, source=80_8D\r\n",
                                g_radar_sensor_seated_hdist);
                    return true;
                }
                break;
            case RADAR_SENSOR_MOTION_HDIST_CMD:
                if (frame->data_len >= 2) {
                    g_radar_sensor_motion_hdist = radar_sensor_read_u16_be(frame->data);
                    g_radar_sensor_hdist_valid = true;
                    osal_printk("[RADAR_SENSOR] motion_horizontal_distance=%u, source=80_8E\r\n",
                                g_radar_sensor_motion_hdist);
                    return true;
                }
                break;
            default:
                break;
        }

        return false;
    }

    if (frame->ctrl != RADAR_SENSOR_FALL_CTRL) {
        return false;
    }

    switch (frame->cmd) {
        case RADAR_SENSOR_FALL_BREAK_HEIGHT_CMD:
            if (frame->data_len >= 2) {
                osal_printk("[RADAR_SENSOR] fall_break_height=%u, source=83_91\r\n",
                            radar_sensor_read_u16_be(frame->data));
                return true;
            }
            break;
        case RADAR_SENSOR_STATIC_RESIDENCY_STATE_CMD:
            if (frame->data_len >= 1) {
                osal_printk("[RADAR_SENSOR] static_residency_state=%u, source=83_85\r\n",
                            frame->data[0]);
                if (frame->data[0] != 0) {
                    radar_sensor_low_posture_evidence("static_residency_state", frame->data[0]);
                }
                return true;
            }
            break;
        case RADAR_SENSOR_STATIC_RESIDENCY_TIME_CMD:
            if (frame->data_len >= 4) {
                uint32_t value = radar_sensor_read_u32_be(frame->data);
                osal_printk("[RADAR_SENSOR] static_residency_time=%u, source=83_8A\r\n",
                            (unsigned int)value);
                if (value != 0) {
                    radar_sensor_low_posture_evidence("static_residency_time", value);
                }
                return true;
            }
            break;
        case RADAR_SENSOR_FALL_SENSITIVITY_CMD:
            if (frame->data_len >= 1) {
                osal_printk("[RADAR_SENSOR] fall_sensitivity=%u, source=83_8D\r\n",
                            frame->data[0]);
                return true;
            }
            break;
        case RADAR_SENSOR_HEIGHT_RATIO_SWITCH_CMD:
            if (frame->data_len >= 1) {
                osal_printk("[RADAR_SENSOR] height_ratio_switch=%u, source=83_95\r\n",
                            frame->data[0]);
                return true;
            }
            break;
        case RADAR_SENSOR_FALL_TIME_CMD:
            if (frame->data_len >= 4) {
                osal_printk("[RADAR_SENSOR] fall_time=%u, source=83_8C\r\n",
                            (unsigned int)radar_sensor_read_u32_be(frame->data));
                return true;
            }
            break;
        case RADAR_SENSOR_HEIGHT_DURATION_CMD:
            if (frame->data_len >= 4) {
                uint32_t value = radar_sensor_read_u32_be(frame->data);
                osal_printk("[RADAR_SENSOR] height_duration=%u, source=83_8F\r\n",
                            (unsigned int)value);
                if (value != 0) {
                    radar_sensor_low_posture_evidence("height_duration", value);
                }
                return true;
            }
            break;
        case RADAR_SENSOR_TRACK_CMD:
            if (frame->data_len >= 4) {
                g_radar_sensor_track_x = radar_sensor_read_u16_be(frame->data);
                g_radar_sensor_track_y = radar_sensor_read_u16_be(&frame->data[2]);
                g_radar_sensor_track_valid = true;
                osal_printk("[RADAR_SENSOR] track_x=%u, track_y=%u, source=83_8E\r\n",
                            g_radar_sensor_track_x, g_radar_sensor_track_y);
                return true;
            }
            break;
        default:
            break;
    }

    return false;
}

static bool radar_sensor_handle_frame(const radar_sensor_frame_t *frame)
{
    if (frame->data_len == 0) {
        return false;
    }

    if ((frame->ctrl == RADAR_SENSOR_ACTIVITY_CTRL) &&
        ((frame->cmd == RADAR_SENSOR_ACTIVITY_QUERY_CMD) ||
         (frame->cmd == RADAR_SENSOR_ACTIVITY_REPORT_CMD))) {
        radar_sensor_update_result(frame->data[0], frame->cmd);
        return true;
    }

    if ((frame->ctrl == RADAR_SENSOR_FALL_CTRL) && (frame->cmd == RADAR_SENSOR_FALL_STATE_CMD)) {
        radar_sensor_update_fall(frame->data[0]);
        return true;
    }

    if (radar_sensor_handle_debug_frame(frame)) {
        return true;
    }

    return false;
}

static errcode_t radar_sensor_uart_init(void)
{
#if defined(CONFIG_PINCTRL_SUPPORT_IE)
    (void)uapi_pin_set_ie(RADAR_SENSOR_UART_RX_PIN, PIN_IE_ENABLE);
#endif
    (void)uapi_pin_set_mode(RADAR_SENSOR_UART_TX_PIN, RADAR_SENSOR_UART_PIN_MODE);
    (void)uapi_pin_set_mode(RADAR_SENSOR_UART_RX_PIN, RADAR_SENSOR_UART_PIN_MODE);

    uart_attr_t attr = {
        .baud_rate = RADAR_SENSOR_UART_BAUD_RATE,
        .data_bits = UART_DATA_BIT_8,
        .stop_bits = UART_STOP_BIT_1,
        .parity = UART_PARITY_NONE,
    };

    uart_pin_config_t pin_config = {
        .tx_pin = RADAR_SENSOR_UART_TX_PIN,
        .rx_pin = RADAR_SENSOR_UART_RX_PIN,
        .cts_pin = PIN_NONE,
        .rts_pin = PIN_NONE,
    };

    (void)uapi_uart_deinit(RADAR_SENSOR_UART_BUS);
    errcode_t ret = uapi_uart_init(RADAR_SENSOR_UART_BUS, &pin_config, &attr, NULL,
                                   &g_radar_sensor_uart_buffer_config);
    if (ret == ERRCODE_SUCC) {
        osal_printk("[RADAR_SENSOR] uart init ok\r\n");
    } else {
        osal_printk("[RADAR_SENSOR] uart init failed, ret=%d\r\n", ret);
    }

    return ret;
}

static bool radar_sensor_read_and_process(uint8_t *read_buffer, uint32_t read_buffer_len)
{
    bool has_valid_frame = false;
    radar_sensor_frame_t frame;

    int32_t read_len = uapi_uart_read(RADAR_SENSOR_UART_BUS, read_buffer, read_buffer_len, 0);
    if (read_len <= 0) {
        return false;
    }

    radar_sensor_cache_append(read_buffer, (uint32_t)read_len);
    while (radar_sensor_parse_frame(&frame)) {
        if (radar_sensor_handle_frame(&frame)) {
            has_valid_frame = true;
        }
    }

    return has_valid_frame;
}

static bool radar_sensor_read_target_frame(uint8_t ctrl, uint8_t cmd, uint8_t *data0,
                                           uint8_t *read_buffer, uint32_t read_buffer_len)
{
    bool found_target = false;
    radar_sensor_frame_t frame;

    int32_t read_len = uapi_uart_read(RADAR_SENSOR_UART_BUS, read_buffer, read_buffer_len, 0);
    if (read_len <= 0) {
        return false;
    }

    radar_sensor_cache_append(read_buffer, (uint32_t)read_len);
    while (radar_sensor_parse_frame(&frame)) {
        if ((frame.ctrl == ctrl) && (frame.cmd == cmd) && (frame.data_len > 0)) {
            *data0 = frame.data[0];
            found_target = true;
        } else {
            (void)radar_sensor_handle_frame(&frame);
        }
    }

    return found_target;
}

static bool radar_sensor_write_query(const uint8_t *cmd, uint32_t cmd_len, const char *name)
{
    int32_t write_len = uapi_uart_write(RADAR_SENSOR_UART_BUS, cmd, cmd_len, 0);
    if (write_len != (int32_t)cmd_len) {
        osal_printk("[RADAR_SENSOR] uart write %s failed, len=%d\r\n", name, write_len);
        return false;
    }
    return true;
}

static bool radar_sensor_write_fall_debug_query(uint32_t index)
{
    switch (index % RADAR_SENSOR_FALL_DEBUG_QUERY_COUNT) {
        case 0:
            return radar_sensor_write_query(g_radar_sensor_height_duration_query,
                                            sizeof(g_radar_sensor_height_duration_query),
                                            "83_8F");
        case 1:
            return radar_sensor_write_query(g_radar_sensor_static_residency_state_query,
                                            sizeof(g_radar_sensor_static_residency_state_query),
                                            "83_85");
        case 2:
            return radar_sensor_write_query(g_radar_sensor_track_query,
                                            sizeof(g_radar_sensor_track_query),
                                            "83_8E");
        case 3:
            return radar_sensor_write_query(g_radar_sensor_static_residency_time_query,
                                            sizeof(g_radar_sensor_static_residency_time_query),
                                            "83_8A");
        case 4:
            return radar_sensor_write_query(g_radar_sensor_seated_hdist_query,
                                            sizeof(g_radar_sensor_seated_hdist_query),
                                            "80_8D");
        case 5:
            return radar_sensor_write_query(g_radar_sensor_motion_hdist_query,
                                            sizeof(g_radar_sensor_motion_hdist_query),
                                            "80_8E");
        case 6:
            return radar_sensor_write_query(g_radar_sensor_fall_break_height_query,
                                            sizeof(g_radar_sensor_fall_break_height_query),
                                            "83_91");
        case 7:
            return radar_sensor_write_query(g_radar_sensor_fall_sensitivity_query,
                                            sizeof(g_radar_sensor_fall_sensitivity_query),
                                            "83_8D");
        case 8:
            return radar_sensor_write_query(g_radar_sensor_height_ratio_switch_query,
                                            sizeof(g_radar_sensor_height_ratio_switch_query),
                                            "83_95");
        default:
            return radar_sensor_write_query(g_radar_sensor_fall_time_query,
                                            sizeof(g_radar_sensor_fall_time_query),
                                            "83_8C");
    }
}

static bool radar_sensor_query_work_mode(uint8_t *mode, uint8_t *read_buffer, uint32_t read_buffer_len)
{
    for (uint32_t i = 0; i < RADAR_SENSOR_MODE_QUERY_RETRY; i++) {
        if (!radar_sensor_write_query(g_radar_sensor_mode_query, sizeof(g_radar_sensor_mode_query), "02_A8")) {
            osal_msleep(RADAR_SENSOR_MODE_RETRY_WAIT_MS);
            continue;
        }

        osal_msleep(RADAR_SENSOR_MODE_RESPONSE_WAIT_MS);
        if (radar_sensor_read_target_frame(RADAR_SENSOR_MODE_CTRL, RADAR_SENSOR_MODE_QUERY_CMD,
                                           mode, read_buffer, read_buffer_len)) {
            return true;
        }
        osal_msleep(RADAR_SENSOR_MODE_RETRY_WAIT_MS);
    }

    return false;
}

static void radar_sensor_ensure_falling_mode(uint8_t *read_buffer, uint32_t read_buffer_len)
{
    uint8_t mode = 0;

    if (!radar_sensor_query_work_mode(&mode, read_buffer, read_buffer_len)) {
        osal_printk("[RADAR_SENSOR] work_mode query failed\r\n");
        return;
    }

    osal_printk("[RADAR_SENSOR] work_mode=%u(%s)\r\n", mode, radar_sensor_work_mode_name(mode));
    if (mode == RADAR_SENSOR_MODE_FALLING) {
        return;
    }

    osal_printk("[RADAR_SENSOR] switch work_mode to falling\r\n");
    if (!radar_sensor_write_query(g_radar_sensor_set_falling_mode, sizeof(g_radar_sensor_set_falling_mode),
                                  "02_08_falling")) {
        return;
    }

    /*
     * DFRobot official driver waits about 10 seconds after work-mode switch.
     * Fall detection may not become valid until the SEN0623 finishes restart.
     */
    osal_msleep(RADAR_SENSOR_MODE_SWITCH_WAIT_MS);

    if (radar_sensor_query_work_mode(&mode, read_buffer, read_buffer_len)) {
        osal_printk("[RADAR_SENSOR] work_mode_after_switch=%u(%s)\r\n",
                    mode, radar_sensor_work_mode_name(mode));
    } else {
        osal_printk("[RADAR_SENSOR] work_mode recheck failed\r\n");
    }
}

static int radar_sensor_task(const char *arg)
{
    unused(arg);

    uint8_t read_buffer[RADAR_SENSOR_READ_BUFFER_SIZE];
    uint32_t query_tick = 0;
    uint32_t fall_debug_query_index = 0;

    if (radar_sensor_uart_init() != ERRCODE_SUCC) {
        osal_printk("[RADAR_SENSOR] task stop\r\n");
        g_radar_sensor_started = false;
        return -1;
    }

    radar_sensor_ensure_falling_mode(read_buffer, sizeof(read_buffer));

    while (1) {
        bool has_valid_frame = false;
        uint32_t slept_ms = 0;

        (void)radar_sensor_write_query(g_radar_sensor_activity_query, sizeof(g_radar_sensor_activity_query),
                                       "80_83");
        osal_msleep(RADAR_SENSOR_RESPONSE_WAIT_MS);
        slept_ms += RADAR_SENSOR_RESPONSE_WAIT_MS;
        has_valid_frame |= radar_sensor_read_and_process(read_buffer, sizeof(read_buffer));

        query_tick++;
        if ((query_tick % RADAR_SENSOR_MODE_RECHECK_INTERVAL_TICKS) == 0) {
            radar_sensor_ensure_falling_mode(read_buffer, sizeof(read_buffer));
            has_valid_frame = true;
        }

        if ((query_tick % RADAR_SENSOR_FALL_QUERY_INTERVAL_TICKS) == 0) {
            (void)radar_sensor_write_query(g_radar_sensor_fall_query, sizeof(g_radar_sensor_fall_query), "83_81");
            osal_msleep(RADAR_SENSOR_RESPONSE_WAIT_MS);
            slept_ms += RADAR_SENSOR_RESPONSE_WAIT_MS;
            has_valid_frame |= radar_sensor_read_and_process(read_buffer, sizeof(read_buffer));
        }

        if ((query_tick % RADAR_SENSOR_FALL_DEBUG_QUERY_INTERVAL_TICKS) == 0) {
            if (radar_sensor_write_fall_debug_query(fall_debug_query_index)) {
                fall_debug_query_index++;
                osal_msleep(RADAR_SENSOR_RESPONSE_WAIT_MS);
                slept_ms += RADAR_SENSOR_RESPONSE_WAIT_MS;
                has_valid_frame |= radar_sensor_read_and_process(read_buffer, sizeof(read_buffer));
            }
        }

        if (!has_valid_frame) {
            radar_sensor_set_offline_if_needed();
        }

        if (g_radar_sensor_motion_wave_pending) {
            radar_sensor_motion_wave_update(g_radar_sensor_motion_wave_pending_level);
            g_radar_sensor_motion_wave_pending = false;
        }

        radar_sensor_fall_rule_tick();
        radar_sensor_refresh_fall_hint(true);

        if (slept_ms < RADAR_SENSOR_QUERY_PERIOD_MS) {
            osal_msleep(RADAR_SENSOR_QUERY_PERIOD_MS - slept_ms);
        }
    }

    return 0;
}

void radar_sensor_start(void)
{
    osal_task *task_handle = NULL;

    if (g_radar_sensor_started) {
        osal_printk("[RADAR_SENSOR] already started\r\n");
        return;
    }

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)radar_sensor_task, NULL, "RadarSensorTask",
                                      RADAR_SENSOR_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        (void)osal_kthread_set_priority(task_handle, RADAR_SENSOR_TASK_PRIO);
        g_radar_sensor_started = true;
        osal_printk("[RADAR_SENSOR] task started\r\n");
    } else {
        osal_printk("[RADAR_SENSOR] task create failed\r\n");
    }
    osal_kthread_unlock();
}
