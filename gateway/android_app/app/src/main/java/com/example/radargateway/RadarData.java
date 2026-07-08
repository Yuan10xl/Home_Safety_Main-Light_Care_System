package com.example.radargateway;

final class RadarData {
    final int motionLevel;
    final String finalResult;
    final int staticTime;
    final int fallResult;
    final int convulsionResult;
    final int lowObserve;
    final int radarOnline;
    final String serviceId;
    final String eventTime;
    final String receivedAt;
    final String topic;
    final String rawJson;

    RadarData(int motionLevel, String finalResult, int staticTime, int fallResult,
              int convulsionResult, int lowObserve, int radarOnline,
              String serviceId, String eventTime,
              String receivedAt, String topic, String rawJson) {
        this.motionLevel = motionLevel;
        this.finalResult = finalResult;
        this.staticTime = staticTime;
        this.fallResult = fallResult;
        this.convulsionResult = convulsionResult;
        this.lowObserve = lowObserve;
        this.radarOnline = radarOnline;
        this.serviceId = serviceId;
        this.eventTime = eventTime;
        this.receivedAt = receivedAt;
        this.topic = topic;
        this.rawJson = rawJson;
    }
}
