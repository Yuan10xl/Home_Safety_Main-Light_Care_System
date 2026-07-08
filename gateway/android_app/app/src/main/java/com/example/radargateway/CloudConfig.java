package com.example.radargateway;

final class CloudConfig {
    static final String ACCESS_KEY_ID = "HPUAAVEVK0MDMRZYOINE";
    static final String SECRET_ACCESS_KEY = "NSQIYoMtNba9O7mk2DEmNifrtWTsKR2N7GE7ris6";

    static final String REGION_ID = "cn-east-3";
    static final String DERIVED_AUTH_SERVICE = "iotda";
    static final String PROJECT_ID = "34b18a5966e2477a948d239185d98140";
    static final String ENDPOINT = "https://e539e002e3.st1.iotda-app.cn-east-3.myhuaweicloud.com";
    static final String DEVICE_ID = "6a48c2ab18855b39c52bdc10_gateway001";
    static final String SERVICE_ID = "GatewayCare";

    static final long POLL_INTERVAL_MS = 2000L;

    private CloudConfig() {
    }
}
