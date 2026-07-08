#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include "errcode.h"
#include "gateway_cloud_config.h"

#define CONFIG_WIFI_SSID GATEWAY_WIFI_SSID
#define CONFIG_WIFI_PWD GATEWAY_WIFI_PASSWORD

errcode_t wifi_connect(void);

#endif /* WIFI_CONNECT_H */
