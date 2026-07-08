#ifndef GATEWAY_HUAWEI_IOT_H
#define GATEWAY_HUAWEI_IOT_H

#ifdef __cplusplus
extern "C" {
#endif

int gateway_huawei_iot_init(void);
int gateway_huawei_iot_publish_property(const char *payload);
int gateway_huawei_iot_publish_event(const char *payload);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_HUAWEI_IOT_H */
