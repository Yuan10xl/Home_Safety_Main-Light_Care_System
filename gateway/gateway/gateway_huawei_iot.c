#include "gateway_huawei_iot.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "MQTTClient.h"
#include "MQTTClientPersistence.h"
#include "cmsis_os2.h"
#include "errcode.h"
#include "gateway_cloud_config.h"
#include "wifi_connect.h"

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

#define GATEWAY_HUAWEI_TOPIC_SIZE 160u
#define GATEWAY_HUAWEI_URI_SIZE 192u
#define GATEWAY_MQTT_KEEP_ALIVE 120
#define GATEWAY_MQTT_CONNECT_RETRY_DELAY 200

static MQTTClient g_gateway_mqtt_client;
static volatile MQTTClient_deliveryToken g_gateway_delivered_token;
static bool g_gateway_mqtt_ready;
static bool g_gateway_mqtt_created;

extern int MQTTClient_init(void);

static int gateway_huawei_iot_build_topic(char *topic,
                                          unsigned int topic_len,
                                          const char *topic_format)
{
    int written;

    if ((topic == NULL) || (topic_format == NULL) || (topic_len == 0u)) {
        return -1;
    }

    written = snprintf(topic, topic_len, topic_format, GATEWAY_HUAWEI_DEVICE_ID);
    if ((written < 0) || ((unsigned int)written >= topic_len)) {
        return -1;
    }

    return 0;
}

static void gateway_mqtt_connection_lost(void *context, char *cause)
{
    (void)context;
    g_gateway_mqtt_ready = false;
    GATEWAY_LOG("[GATEWAY] huawei mqtt connection lost: %s\r\n",
                (cause == NULL) ? "unknown" : cause);
}

static void gateway_mqtt_delivered(void *context, MQTTClient_deliveryToken token)
{
    (void)context;
    g_gateway_delivered_token = token;
    GATEWAY_LOG("[GATEWAY] huawei mqtt delivered token=%d\r\n", token);
}

static int gateway_mqtt_message_arrived(void *context,
                                        char *topic_name,
                                        int topic_len,
                                        MQTTClient_message *message)
{
    (void)context;
    (void)topic_len;
    GATEWAY_LOG("[GATEWAY] huawei mqtt message topic=%s len=%d\r\n",
                (topic_name == NULL) ? "" : topic_name,
                (message == NULL) ? 0 : message->payloadlen);
    return 1;
}

static int gateway_huawei_iot_mqtt_connect(void)
{
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
#if (GATEWAY_HUAWEI_IOT_PORT == 8883)
    MQTTClient_SSLOptions ssl_opts = MQTTClient_SSLOptions_initializer;
#endif
    char server_uri[GATEWAY_HUAWEI_URI_SIZE];
    int written;
    int ret;

    MQTTClient_init();

    if (!g_gateway_mqtt_created) {
#if (GATEWAY_HUAWEI_IOT_PORT == 8883)
        const char *uri_scheme = "ssl";
        written = snprintf(server_uri,
                           sizeof(server_uri),
                           "%s://%s:%u",
                           uri_scheme,
                           GATEWAY_HUAWEI_IOT_ENDPOINT,
                           (unsigned int)GATEWAY_HUAWEI_IOT_PORT);
#else
        written = snprintf(server_uri,
                           sizeof(server_uri),
                           "%s",
                           GATEWAY_HUAWEI_IOT_ENDPOINT);
#endif
        if ((written < 0) || ((unsigned int)written >= sizeof(server_uri))) {
            GATEWAY_LOG("[GATEWAY] huawei mqtt uri too long\r\n");
            return -1;
        }

        ret = MQTTClient_create(&g_gateway_mqtt_client,
                                server_uri,
                                GATEWAY_HUAWEI_CLIENT_ID,
                                MQTTCLIENT_PERSISTENCE_NONE,
                                NULL);
        if (ret != MQTTCLIENT_SUCCESS) {
            GATEWAY_LOG("[GATEWAY] huawei mqtt create failed ret=%d\r\n", ret);
            return -1;
        }
        g_gateway_mqtt_created = true;
        MQTTClient_setCallbacks(g_gateway_mqtt_client,
                                NULL,
                                gateway_mqtt_connection_lost,
                                gateway_mqtt_message_arrived,
                                gateway_mqtt_delivered);
    }

    conn_opts.keepAliveInterval = GATEWAY_MQTT_KEEP_ALIVE;
    conn_opts.cleansession = 1;
    conn_opts.username = GATEWAY_HUAWEI_USERNAME;
    conn_opts.password = GATEWAY_HUAWEI_PASSWORD;
#if (GATEWAY_HUAWEI_IOT_PORT == 8883)
    ssl_opts.enableServerCertAuth = GATEWAY_HUAWEI_TLS_ENABLE_SERVER_CERT_AUTH;
    conn_opts.ssl = &ssl_opts;
#endif

    ret = MQTTClient_connect(g_gateway_mqtt_client, &conn_opts);
    if (ret != MQTTCLIENT_SUCCESS) {
        GATEWAY_LOG("[GATEWAY] huawei mqtt connect failed ret=%d\r\n", ret);
        g_gateway_mqtt_ready = false;
        return -1;
    }

    g_gateway_mqtt_ready = true;
    GATEWAY_LOG("[GATEWAY] huawei mqtt connected protocol=%s endpoint=%s port=%u device_id=%s\r\n",
                GATEWAY_HUAWEI_IOT_PROTOCOL,
                GATEWAY_HUAWEI_IOT_ENDPOINT,
                (unsigned int)GATEWAY_HUAWEI_IOT_PORT,
                GATEWAY_HUAWEI_DEVICE_ID);
    return 0;
}

static int gateway_huawei_iot_mqtt_publish(const char *topic, const char *payload)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret;

    if ((topic == NULL) || (payload == NULL)) {
        return -1;
    }

    if (!g_gateway_mqtt_ready) {
        GATEWAY_LOG("[GATEWAY] huawei mqtt not ready, skip topic=%s payload=%s\r\n",
                    topic,
                    payload);
        return -1;
    }

    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = (int)strlen(payload);
    pubmsg.qos = 1;
    pubmsg.retained = 0;

    GATEWAY_LOG("[GATEWAY] huawei mqtt publish topic=%s payload=%s\r\n", topic, payload);
    ret = MQTTClient_publishMessage(g_gateway_mqtt_client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        GATEWAY_LOG("[GATEWAY] huawei mqtt publish failed ret=%d\r\n", ret);
        return -1;
    }

    return 0;
}

int gateway_huawei_iot_init(void)
{
    GATEWAY_LOG("[GATEWAY] huawei iot init protocol=%s endpoint=%s port=%u device_id=%s\r\n",
                GATEWAY_HUAWEI_IOT_PROTOCOL,
                GATEWAY_HUAWEI_IOT_ENDPOINT,
                (unsigned int)GATEWAY_HUAWEI_IOT_PORT,
                GATEWAY_HUAWEI_DEVICE_ID);

    if (wifi_connect() != ERRCODE_SUCC) {
        GATEWAY_LOG("[GATEWAY] wifi connect failed, cloud disabled\r\n");
        return -1;
    }

    for (;;) {
        if (gateway_huawei_iot_mqtt_connect() == 0) {
            return 0;
        }
        (void)osDelay(GATEWAY_MQTT_CONNECT_RETRY_DELAY);
    }
}

int gateway_huawei_iot_publish_property(const char *payload)
{
    char topic[GATEWAY_HUAWEI_TOPIC_SIZE];

    if (payload == NULL) {
        return -1;
    }

    if (gateway_huawei_iot_build_topic(topic,
                                       sizeof(topic),
                                       GATEWAY_CLOUD_PROPERTY_TOPIC_FORMAT) != 0) {
        GATEWAY_LOG("[GATEWAY] huawei property topic build failed\r\n");
        return -1;
    }

    return gateway_huawei_iot_mqtt_publish(topic, payload);
}

int gateway_huawei_iot_publish_event(const char *payload)
{
    char topic[GATEWAY_HUAWEI_TOPIC_SIZE];

    if (payload == NULL) {
        return -1;
    }

    if (gateway_huawei_iot_build_topic(topic,
                                       sizeof(topic),
                                       GATEWAY_CLOUD_EVENT_TOPIC_FORMAT) != 0) {
        GATEWAY_LOG("[GATEWAY] huawei event topic build failed\r\n");
        return -1;
    }

    return gateway_huawei_iot_mqtt_publish(topic, payload);
}
