# SLE radar gateway + Huawei Cloud IoTDA

This sample integrates the gateway delivery package into the WS63 LiteOS app.

## What it does

- Receives `radar_light_packet_t` bytes through a WS63 SLE server write callback.
- Handles split or coalesced SLE byte streams with a small fixed-size buffer.
- Decodes packet version and CRC with `common/radar_protocol`.
- Runs gateway-side fall and abnormal activity wave detection.
- Uploads `GatewayCare` properties and events to Huawei Cloud IoTDA over MQTT.

## Enable the sample

Build with:

```text
-def=CONFIG_SAMPLE_SUPPORT_GATEWAY_HUAWEI_CLOUD=1,CONFIG_ENABLE_WIFI_SAMPLE=1
```

If another sample is enabled in the same image, disable it to avoid multiple
`app_run()` entries doing unrelated work.

## Required parameters

Update `gateway/gateway_cloud_config.h` before flashing:

- `GATEWAY_WIFI_SSID`
- `GATEWAY_WIFI_PASSWORD`
- `GATEWAY_HUAWEI_IOT_ENDPOINT`
- `GATEWAY_HUAWEI_IOT_PORT`
- `GATEWAY_HUAWEI_IOT_PROTOCOL`
- `GATEWAY_HUAWEI_DEVICE_ID`
- `GATEWAY_HUAWEI_CLIENT_ID`
- `GATEWAY_HUAWEI_USERNAME`
- `GATEWAY_HUAWEI_PASSWORD`

The current configuration uses Huawei Cloud IoTDA MQTTS on port `8883`. The
sample creates the Paho client with `ssl://hostname:8883`. Server certificate
verification is disabled by default through
`GATEWAY_HUAWEI_TLS_ENABLE_SERVER_CERT_AUTH=0`; enable it only after adding the
Huawei Cloud root CA to the Paho SSL options.

## Expected serial logs

```text
[GATEWAY] runtime start
[GATEWAY] sle server ready ...
[GATEWAY] sle rx init ready
[GATEWAY] huawei mqtt connected protocol=MQTTS ...
[GATEWAY] sle packet received len=...
[GATEWAY] packet decode ok seq=... node=...
[GATEWAY] huawei mqtt publish topic=$oc/devices/.../sys/properties/report payload=...
```

For bad packets:

```text
[GATEWAY] packet drop reason=crc
[GATEWAY] packet drop reason=version
```
