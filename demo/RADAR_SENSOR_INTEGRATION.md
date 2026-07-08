# SEN0623 / C1001 Radar Sensor Integration

This module is the formal WS63E radar-board wrapper for SEN0623 / C1001.
In the two-board final design, this board is the radar node. It starts the
radar driver, builds a lightweight radar packet every 500 ms, and hands that
packet to the SLE transmit wrapper. The gateway board owns final event
judgment, light control, cloud, and app integration.

## Files

- `radar_sensor.c`
- `radar_sensor.h`
- `radar_node_app.c`
- `radar_node/*`
- `common/radar_protocol/*`

Old `sen0623_test.c/.h` is kept only as a compatibility wrapper and does not register `app_run()`.

## Radar-Board App Entry

Formal radar-board firmware uses:

```c
app_run(radar_node_app_start);
```

`radar_node_app_start` creates `RadarNodeApp`, then:

1. calls `radar_sensor_start()`;
2. calls `radar_node_init(1)`;
3. reads `radar_sensor_get_result()` every 500 ms;
4. converts the result into `radar_light_packet_t`;
5. calls `radar_sle_send_packet()`.

The old self-test entry is disabled by default:

```c
#define RADAR_SENSOR_SELF_TEST_ENABLE 0
```

## Driver API

Include:

```c
#include "radar_sensor.h"
```

Start once from the main-control task or unified app entry:

```c
radar_sensor_start();
```

Read latest result periodically:

```c
const radar_result_t *radar = radar_sensor_get_result();
```

Useful first-stage checks:

```c
if (radar->suspect_fall_hint) {
    /* Composite suspected fall risk hint. Ask family to confirm. */
}

if (radar->static_time >= RADAR_SENSOR_ABNORMAL_LOW_ACTIVITY_SECONDS) {
    /* Suspected abnormal low activity. Main-control decides whether this
     * should become an area-stay event for the installed lamp/radar position.
     */
}
```

## Radar Teammate Standalone Test

The formal driver does not register `app_run()`. For radar-side standalone testing, temporarily enable the self-test file:

1. Open `radar_sensor_selftest.c`.
2. Change `RADAR_SENSOR_SELF_TEST_ENABLE` from `0` to `1`.
3. Build and flash `ws63-liteos-app`.
4. Check the serial log for:

```text
[RADAR_SELFTEST] task created
[RADAR_SELFTEST] start
[RADAR_SENSOR] task started
[RADAR_SENSOR] uart init ok
[RADAR_SENSOR] work_mode=1(falling)
[RADAR_SENSOR] motion_level=xx, source=80_83
[RADAR_SENSOR] official_fall_hint=0, source=83_81
[RADAR_SELFTEST] has_person=1, motion_level=xx, static_time=xx, ...
```

Move near the radar to confirm `motion_level` rises. Keep still after obvious activity to confirm `static_time` increases. Before handing code to main-control, change `RADAR_SENSOR_SELF_TEST_ENABLE` back to `0`.

Radar-board integration test is different: keep self-test disabled, let
`radar_node_app.c` start the driver and send feature packets periodically.

## Radar Light Packet

The radar node uses the shared protocol files under `common/radar_protocol/`.
These files must be identical on the radar board and gateway board.

Current wire format:

- protocol version: `RADAR_LIGHT_PACKET_VERSION = 1`
- wire length: `RADAR_LIGHT_PACKET_WIRE_SIZE = 27`
- endian: little-endian
- CRC: CRC16-CCITT over the first 25 bytes

Important field notes:

- `human_present` means the radar driver currently believes a person or recent
  radar presence exists. It is not a link-online flag.
- `motion_level` comes from SEN0623 `activity_level`, recommended range 0-100.
- `static_duration_ms` is converted from `radar_result_t.static_time` seconds.
- `fall_feature_*` is a suspected-fall feature hint for the gateway. It is not
  a confirmed fall event.
- `convulsion_feature_*` is only an abnormal-activity-wave feature hint. Public
  wording should be "suspected abnormal activity wave", not a diagnosis.
- `radar_status` is set from radar-node health, not from `human_present`.
  Empty room / no person must still be reported as `RADAR_STATUS_ONLINE`.

`radar_sle_tx.c` is the only place that should bind the real WS63E SLE send API.
Until that binding is finished, packet encoding can be verified from
`[RADAR_NODE]` logs, but the gateway will not receive real SLE packets.

## Hardware

- SEN0623 TX -> WS63E RXD1(GPIO_16)
- SEN0623 RX -> WS63E TXD1(GPIO_15)
- SEN0623 VIN -> WS63E 5V
- SEN0623 GND -> WS63E GND

UART configuration:

- `UART_BUS_1`
- TXD1 = `GPIO_15`
- RXD1 = `GPIO_16`
- `PIN_MODE_1`
- 115200 8N1
- No flow control

## Parsed Frames

Motion level comes from SEN0623 `activity_level`:

```text
53 59 80 03 00 01 XX CHECK 54 43
53 59 80 83 00 01 XX CHECK 54 43
```

`XX` is mapped to `radar_result_t.motion_level`, range 0-100.

The official fall hint still comes from SEN0623 `83_81`:

```text
Query : 53 59 83 81 00 01 0F C0 54 43
Return: 53 59 83 81 00 01 XX CHECK 54 43
```

`XX=1` sets the internal `official_fall_hint=1`. `XX=0` clears the
official source. SEN0623 fall detection is intended for top-down/overhead
installation; desktop side placement may not trigger reliably.

`radar_result_t.suspect_fall_hint` is now a composite suspected fall risk hint.
It is set when either source below is active, and it is held for about 10
seconds after a trigger so main-control polling is less likely to miss it:

1. SEN0623 official `83_81` source.
2. WS63E local auxiliary rule: previous obvious activity has been seen,
   low-posture hold is at least 10 seconds, horizontal movement is within the
   threshold. Low activity is not used as a fall condition; it remains a
   separate abnormal-low-activity / area-stay input for the rule layer.
3. Both sources at the same time.

This is not a confirmed fall or medical diagnosis. It only means the system
should ask family/caregivers to confirm.

Source logs are explicit:

```text
[RADAR_ALERT] suspect_fall_hint source=official_83_81
[RADAR_ALERT] suspect_fall_hint source=rule_low_posture low_hold=xx horizontal_delta=xx static_time=xx motion_level=xx
[RADAR_ALERT] suspect_fall_hint source=both low_hold=xx horizontal_delta=xx static_time=xx motion_level=xx
```

The driver also checks SEN0623 work mode on startup:

```text
Query current mode : 53 59 02 A8 00 01 0F 66 54 43
Set falling mode   : 53 59 02 08 00 01 01 B8 54 43
```

Expected mode log:

```text
[RADAR_SENSOR] work_mode=1(falling)
```

If the module was not already in falling mode, the driver logs:

```text
[RADAR_SENSOR] switch work_mode to falling
[RADAR_SENSOR] work_mode_after_switch=1(falling)
```

The switch follows the DFRobot reference behavior and waits about 10 seconds for the sensor to restart. During this time, fall polling has not started yet.

Observed threshold notes:

- Static/no-person background is mostly `motion_level` 1-2.
- A still person or light disturbance is also mostly 1-2, but can occasionally reach 6-9.
- `motion_level <= 10` is the engineering low-activity range.
- `motion_level > 20` is obvious activity.
- `motion_level > 40` is a strong-activity reference.

## Result Mapping

- `has_person`: conservative first-stage value. Recent valid radar frames set it to 1. Long periods without valid frames set it to 0. Main control should prefer `motion_level` and `static_time`.
- `motion_level`: SEN0623 activity_level.
- `static_time`: low-activity duration in seconds. Internally accumulated from 500 ms task ticks after obvious activity has been seen.
- `area_id`: always `AREA_NORMAL` in this version. SEN0623 area mapping is not enabled here.
- `stay_time`: always 0 in this version. Do not treat `static_time` as real area stay time.
- `suspect_fall_hint`: composite suspected fall risk hint. It may come from
  SEN0623 official `83_81`, the WS63E local low-posture/stable-position rule
  after obvious activity, or both. A triggered hint is held for about 10 seconds.
- `area_stay_hint`: always 0 in this version. Internal `abnormal_low_activity` is only logged and is not mapped to an area-stay event.

Current `area_stay_hint` is fixed to 0 because the SEN0623 first-stage driver does not provide a real area id. If main-control needs to trigger "suspected abnormal low activity" in the first stage, use a `static_time` threshold, for example `static_time >= 10` seconds. Whether this should be mapped to an area-stay event is decided by the main-control rule layer.

## Suspected Abnormal Motion Wave

The driver now logs a debug/first-stage suspected abnormal activity-wave hint:

```text
[RADAR_ALERT] suspect_abnormal_motion_wave
```

This is not exposed in `radar_result_t` yet. It should be described only as
"suspected abnormal activity wave / convulsion-like risk feature / family
confirmation needed", not as a medical diagnosis.

The logic is gated before waveform analysis:

- Any of these conditions opens a short `wave_watch_ticks` observation window:
  `suspect_fall_hint == 1`, recent `fall_rule_candidate`,
  low-posture hold >= 3 seconds, or `static_time >= 3` seconds. A future
  main-control scene gate can be added here, but the current driver keeps that
  reserved and false.
- Opening the watch window resets the motion-wave window and starts a short
  warmup period. This prevents peaks collected before `watch_start` from
  immediately triggering `suspect_abnormal_motion_wave`.
- While `wave_watch_ticks > 0`, `risk_context` stays true. This is intentional:
  if `static_time >= 3` starts observation and later `active_recovered` clears
  `static_time`, the driver still evaluates the following low-high-low
  fluctuation inside the watch window.
- `wave_watch_ticks` is decremented by the 500 ms motion-sample processing
  cadence. After it reaches 0, `risk_context` returns to 0 unless a new risk
  reason starts another watch window. If `fall_rule_reset` / low-posture loss
  happens and there is no remaining risk reason (`static_time < 3`, no
  low-position context, and `suspect_fall_hint == 0`), the driver clears the
  watch window early.
- `active_recovered` does not clear the motion-wave window. The window is reset
  when the watch window ends and there is no new risk reason, when the radar is
  offline, or when a fall-rule rejection occurs with no active watch context.
- One watch window prints at most one
  `[RADAR_ALERT] suspect_abnormal_motion_wave`. After an alert, a 10-second
  alert cooldown suppresses repeated RADAR_ALERT logs across adjacent windows;
  `[RADAR_WAVE] amp=...` metrics may still be printed during cooldown.
- Normal walking / continuous activity is excluded first. If more than 75% of
  the 10-second window is `motion_level > 20`, or the longest continuous
  `>20` run is longer than 4 seconds, the driver treats it as normal continuous
  activity and does not alert.
- The 10-second window uses 20 samples at the 500 ms task cadence. It checks
  amplitude, low/high crossing count, local peak count, strong activity count,
  high-activity ratio, and longest high-activity run.

Current trigger thresholds:

```text
amplitude >= 20
cross_count >= 6
peak_count >= 3
strong_count >= 2
high_ratio <= 0.75
max_high_run <= 8 ticks
risk context is true
```

The driver prints the watch and metrics logs:

```text
[RADAR_WAVE] watch_start reason=static_time reset_window=1
[RADAR_WAVE] warmup ticks=x
[RADAR_WAVE] amp=xx cross=xx peak=xx high_ratio=xx max_high_run=xx risk=xx
[RADAR_WAVE] watch_clear reason=low_posture_lost
```

If the 10-second waveform looks like abnormal motion fluctuation but
`risk_context` is false, the driver logs only:

```text
[RADAR_DEBUG] motion_wave_detected_but_no_risk_context
```

This debug line means "ordinary fluctuation observed, not an alert".

The web dashboards parse this log and show it as a suspected abnormal activity
wave. If main-control later wants this as a formal field, discuss adding a new
`suspect_motion_wave_hint` field instead of reusing `area_stay_hint`.

## Internal First-Version Logic

- `motion_level > RADAR_SENSOR_ACTIVE_THRESHOLD` for 2 consecutive valid samples: obvious activity.
- After obvious activity, `motion_level <= RADAR_SENSOR_LOW_ACTIVITY_THRESHOLD` accumulates `static_time`.
- When low activity reaches the internal threshold, the driver logs:

```text
[RADAR_SENSOR] abnormal_low_activity, static_time=10, area_stay_hint=0
```

The unified interface does not add non-standard fields for this internal hint.
