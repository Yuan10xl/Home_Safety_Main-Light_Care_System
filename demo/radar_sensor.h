#ifndef RADAR_SENSOR_H
#define RADAR_SENSOR_H

#include <stdint.h>

/*
 * SEN0623 / C1001 radar sensor driver for WS63E.
 *
 * Hardware:
 *   SEN0623 TX  -> WS63E RXD1(GPIO_16)
 *   SEN0623 RX  -> WS63E TXD1(GPIO_15)
 *   SEN0623 VIN -> WS63E 5V
 *   SEN0623 GND -> WS63E GND
 *
 * UART:
 *   UART_BUS_1, 115200, 8N1, no flow control.
 *
 * Current reliable fields:
 *   motion_level: SEN0623 activity_level, 0-100.
 *   static_time : low-activity duration in seconds. Internally accumulated
 *                 from the 500 ms radar polling task tick.
 *   suspect_fall_hint: composite suspected fall risk hint. It may come from
 *                 SEN0623 official 83_81, the WS63E local low-posture /
 *                 stable-position auxiliary rule after obvious activity, or both.
 *                 A triggered hint is held for about 10 seconds. It is not
 *                 a medical diagnosis or confirmed fall event.
 *
 * Field-observed motion_level notes:
 *   Static/no-person background is mostly 1-2.
 *   A still person or light disturbance is also mostly 1-2, but can
 *   occasionally reach 6-9.
 *   Engineering thresholds:
 *     motion_level <= 10: low activity range.
 *     motion_level > 20 : obvious activity.
 *     motion_level > 40 : strong activity reference.
 *
 * Current first-version limitations:
 *   area_id and stay_time are not real SEN0623 area results. area_id stays
 *   AREA_NORMAL and stay_time stays 0.
 *   area_stay_hint stays 0. SEN0623 first-stage integration does not provide
 *   a stable real area id, so low activity is not mapped to area_stay_hint
 *   here. Main-control should use motion_level/static_time and its own
 *   installation-area rules if it wants to create an area-stay event.
 *
 * Fall detection installation note:
 *   SEN0623 fall detection is intended for top-down/overhead installation.
 *   Desktop side placement may not trigger 83_81 reliably.
 *   The driver queries work mode with 02_A8 on startup. If the module is not
 *   in falling mode, it sends 02_08 to switch to falling mode and waits for
 *   the sensor to restart before polling 83_81.
 *   For fall-scene testing, radar_sensor.c also prints low-rate debug-only
 *   SEN0623 fall-mode fields such as 83_91 fall_break_height, 83_85
 *   static_residency_state, 83_8A static_residency_time, 83_8D
 *   fall_sensitivity, 83_95 height_ratio_switch, 83_8C fall_time, and 83_8F
 *   height_duration. These logs are not exposed through radar_result_t yet;
 *   they are used to validate whether low posture / ground disturbance can be
 *   detected reliably before changing the main-control interface.
 *   A fall-rule state machine logs low-posture hold candidates using
 *   low-posture evidence plus horizontal stability. The final
 *   suspect_fall_hint output is a composite of the official 83_81 hint and
 *   this local auxiliary rule, with explicit source logs.
 *
 * has_person note:
 *   First version uses a conservative data-presence strategy: recent valid
 *   radar frames set has_person to 1, and long no-valid-frame periods set it
 *   to 0. It is not the main first-stage judgment. Main control should prefer
 *   motion_level and static_time for behavior decisions.
 *
 * Abnormal motion-wave note:
 *   The driver can log a first-stage suspected abnormal activity-wave hint
 *   with "[RADAR_ALERT] suspect_abnormal_motion_wave". SEN0623 83_81 fall
 *   hint, a recent fall-rule candidate, low-posture hold >= 3 seconds, or
 *   static_time >= 3 seconds opens a short wave_watch_ticks observation
 *   window. Opening the watch window resets the motion-wave window and starts
 *   a short warmup, so activity peaks before watch_start are not reused for an
 *   immediate alert. During the watch window, risk context stays true even if
 *   active_recovered clears static_time, so low-high-low fluctuation after a
 *   quiet period can still be evaluated. Normal continuous activity is
 *   excluded before checking the 10-second motion_level waveform. One watch
 *   window reports at most one RADAR_ALERT, with a 10-second alert cooldown
 *   across adjacent windows. The hint is for family confirmation / debugging,
 *   not a medical diagnosis, and it is not exposed in radar_result_t yet.
 */

#define AREA_NORMAL 0
#define AREA_RISK   1
#define AREA_BED    2

#define RADAR_SENSOR_LOW_ACTIVITY_THRESHOLD 10
#define RADAR_SENSOR_ACTIVE_THRESHOLD       20
#define RADAR_SENSOR_STRONG_THRESHOLD       40
#define RADAR_SENSOR_ABNORMAL_LOW_ACTIVITY_SECONDS 10

typedef struct {
    int has_person;
    int motion_level;
    int static_time;
    int area_id;
    int stay_time;
    int suspect_fall_hint;
    int area_stay_hint;
} radar_result_t;

#define RADAR_SENSOR_FEATURE_FALL_OFFICIAL          0x01u
#define RADAR_SENSOR_FEATURE_FALL_LOCAL             0x02u
#define RADAR_SENSOR_FEATURE_MOTION_WAVE_HINT       0x04u
#define RADAR_SENSOR_FEATURE_MOTION_WAVE_RISK       0x08u
#define RADAR_SENSOR_FEATURE_LOW_POSTURE_CONTEXT    0x10u
#define RADAR_SENSOR_FEATURE_FALL_CANDIDATE_RECENT  0x20u

typedef struct {
    uint32_t motion_sample_seq;
    uint8_t feature_flags;
} radar_sensor_feature_state_t;

void radar_sensor_start(void);
const radar_result_t *radar_sensor_get_result(void);
void radar_sensor_get_feature_state(radar_sensor_feature_state_t *state);

#endif /* RADAR_SENSOR_H */
