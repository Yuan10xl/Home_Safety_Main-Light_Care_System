# Radar Gateway Frontend

This page shows parsed radar gateway data from serial logs, pasted Huawei IoT JSON, or a WebSocket stream.

## Live serial

Open the page from localhost, then use Chrome or Edge:

```powershell
cd C:\Users\ecydm\Downloads\fbb_ws63-master\src\application\samples\custom\gateway_huawei_cloud\frontend
py -3 -m http.server 8787 --bind 127.0.0.1
```

Then open:

```text
http://127.0.0.1:8787
```

Click `Connect Serial`, choose the gateway COM port, and use baud rate `115200`.

## Expected gateway logs

The page updates when it sees logs like:

```text
[GATEWAY] notify subscribed status=0x0
[GATEWAY] rx len=27
[GATEWAY] crc ok
[GATEWAY] packet seq=18 motion=42 static=1500
[GATEWAY] huawei mqtt connected ...
```

It also parses Huawei IoT property payloads containing `motion_level` and `final_result`.

## Debug export

`Export CSV` now exports full debug rows instead of only `time,motion`. Important columns include:

- packet fields: `packet_seq`, `motion_level`, `static_time_ms`
- fall fields: `fall_result`, `fall_reason`, `low_observe`
- abnormal wave fields: `convulsion_result`, `convulsion_reason`, `amp`, `cross`, `peak`, `strong`, `high_ratio`, `max_high_run`
- link/cloud fields: `radar_online`, `serial_state`, `scan_state`, `sle_state`, `notify_state`, `cloud_state`
- raw source lines: `raw_packet_line`, `raw_fall_line`, `raw_convulsion_line`, `raw_cloud_line`

The `Raw Gateway Logs` panel displays only the latest 500 important gateway serial lines to keep the browser responsive, but the page keeps the complete log in memory from the start of the test. Use `Export Logs` when a test does not trigger as expected; it exports all saved raw log lines, not just the visible lines. Send both the CSV and raw log text for threshold analysis.
