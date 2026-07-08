#ifndef GATEWAY_CLOUD_CONFIG_H
#define GATEWAY_CLOUD_CONFIG_H

/*
 * Huawei Cloud IoTDA product model recommendation:
 *
 * service_id: GatewayCare
 * property:
 *   - motion_level: int
 *   - final_result: string
 *   - static_time: int, milliseconds
 *   - fall_result: int, 0/1
 *   - convulsion_result: int, 0/1
 *   - low_observe: int, 0/1
 *   - radar_online: int, 0/1
 * events:
 *   - suspected_fall
 *   - suspected_abnormal_activity_wave
 */
#define GATEWAY_CLOUD_SERVICE_ID "GatewayCare"

/*
 * Fill these values from Huawei Cloud IoTDA device detail page:
 *
 * 1. Open the device.
 * 2. Click "MQTT连接参数 -> 查看".
 * 3. Copy the complete values into the macros below.
 *
 * Do not put the device secret in app-side code. It belongs only on the
 * gateway device.
 */
#ifndef GATEWAY_WIFI_SSID
#define GATEWAY_WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef GATEWAY_WIFI_PASSWORD
#define GATEWAY_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef GATEWAY_HUAWEI_IOT_ENDPOINT
#define GATEWAY_HUAWEI_IOT_ENDPOINT "YOUR_IOTDA_ENDPOINT"
#endif

#ifndef GATEWAY_HUAWEI_IOT_PORT
#define GATEWAY_HUAWEI_IOT_PORT 1883
#endif

#ifndef GATEWAY_HUAWEI_IOT_PROTOCOL
#define GATEWAY_HUAWEI_IOT_PROTOCOL "MQTT"
#endif

/*
 * 0: use TLS transport but do not verify the server certificate.
 * 1: verify the server certificate. If enabled, provide trustStore or
 *    los_trustStore in gateway_huawei_iot.c.
 */
#ifndef GATEWAY_HUAWEI_TLS_ENABLE_SERVER_CERT_AUTH
#define GATEWAY_HUAWEI_TLS_ENABLE_SERVER_CERT_AUTH 0
#endif

#ifndef GATEWAY_HUAWEI_DEVICE_ID
#define GATEWAY_HUAWEI_DEVICE_ID "YOUR_DEVICE_ID"
#endif

#ifndef GATEWAY_HUAWEI_CLIENT_ID
#define GATEWAY_HUAWEI_CLIENT_ID "YOUR_CLIENT_ID"
#endif

#ifndef GATEWAY_HUAWEI_USERNAME
#define GATEWAY_HUAWEI_USERNAME "YOUR_USERNAME"
#endif

#ifndef GATEWAY_HUAWEI_PASSWORD
#define GATEWAY_HUAWEI_PASSWORD "YOUR_DEVICE_SECRET_OR_MQTT_PASSWORD"
#endif

#define GATEWAY_CLOUD_PROPERTY_TOPIC_FORMAT \
    "$oc/devices/%s/sys/properties/report"
#define GATEWAY_CLOUD_EVENT_TOPIC_FORMAT \
    "$oc/devices/%s/sys/events/up"

#endif /* GATEWAY_CLOUD_CONFIG_H */
