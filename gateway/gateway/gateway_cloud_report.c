#include "gateway_cloud_report.h"

#include <stdio.h>

#include "gateway_cloud_config.h"
#include "gateway_huawei_iot.h"

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

#define GATEWAY_CLOUD_PAYLOAD_SIZE 512u

gateway_cloud_result_t gateway_cloud_select_result(
    const fall_result_t *fall_result,
    const convulsion_result_t *convulsion_result)
{
    if ((fall_result != NULL) && (fall_result->is_suspected_fall != 0u)) {
        return GATEWAY_CLOUD_RESULT_SUSPECTED_FALL;
    }

    if ((convulsion_result != NULL) &&
        (convulsion_result->is_suspected_convulsion != 0u)) {
        return GATEWAY_CLOUD_RESULT_SUSPECTED_ABNORMAL_ACTIVITY_WAVE;
    }

    if ((fall_result != NULL) && (fall_result->is_low_activity_observe != 0u)) {
        return GATEWAY_CLOUD_RESULT_LOW_ACTIVITY_OBSERVE;
    }

    return GATEWAY_CLOUD_RESULT_NORMAL;
}

const char *gateway_cloud_result_name(gateway_cloud_result_t result)
{
    switch (result) {
        case GATEWAY_CLOUD_RESULT_NORMAL:
            return "normal";
        case GATEWAY_CLOUD_RESULT_LOW_ACTIVITY_OBSERVE:
            return "low_activity_observe";
        case GATEWAY_CLOUD_RESULT_SUSPECTED_FALL:
            return "suspected_fall";
        case GATEWAY_CLOUD_RESULT_SUSPECTED_ABNORMAL_ACTIVITY_WAVE:
            return "suspected_abnormal_activity_wave";
        default:
            return "unknown";
    }
}

void gateway_cloud_report_status(const gateway_cloud_status_t *status)
{
    char payload[GATEWAY_CLOUD_PAYLOAD_SIZE];
    int written;

    if (status == NULL) {
        return;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"services\":[{\"service_id\":\"%s\","
                       "\"properties\":{\"motion_level\":%u,"
                       "\"final_result\":\"%s\","
                       "\"static_time\":%u,"
                       "\"fall_result\":%u,"
                       "\"convulsion_result\":%u,"
                       "\"low_observe\":%u,"
                       "\"radar_online\":%u}}]}",
                       GATEWAY_CLOUD_SERVICE_ID,
                       (unsigned int)status->motion_level,
                       gateway_cloud_result_name(status->final_result),
                       (unsigned int)status->static_time,
                       (unsigned int)status->fall_result,
                       (unsigned int)status->convulsion_result,
                       (unsigned int)status->low_observe,
                       (unsigned int)status->radar_online);
    if ((written < 0) || ((unsigned int)written >= sizeof(payload))) {
        GATEWAY_LOG("[GATEWAY] cloud status payload too long\r\n");
        return;
    }

    (void)gateway_huawei_iot_publish_property(payload);
}

void gateway_cloud_report_event(const gateway_cloud_status_t *status,
                                gateway_cloud_result_t result)
{
    char payload[GATEWAY_CLOUD_PAYLOAD_SIZE];
    const char *result_name = gateway_cloud_result_name(result);
    int written;

    if ((status == NULL) || (result == GATEWAY_CLOUD_RESULT_NORMAL) ||
        (result == GATEWAY_CLOUD_RESULT_LOW_ACTIVITY_OBSERVE)) {
        return;
    }

    written = snprintf(payload,
                       sizeof(payload),
                       "{\"services\":[{\"service_id\":\"%s\","
                       "\"event_type\":\"%s\","
                       "\"paras\":{\"motion_level\":%u,"
                       "\"final_result\":\"%s\","
                       "\"static_time\":%u,"
                       "\"fall_result\":%u,"
                       "\"convulsion_result\":%u,"
                       "\"low_observe\":%u,"
                       "\"radar_online\":%u}}]}",
                       GATEWAY_CLOUD_SERVICE_ID,
                       result_name,
                       (unsigned int)status->motion_level,
                       result_name,
                       (unsigned int)status->static_time,
                       (unsigned int)status->fall_result,
                       (unsigned int)status->convulsion_result,
                       (unsigned int)status->low_observe,
                       (unsigned int)status->radar_online);
    if ((written < 0) || ((unsigned int)written >= sizeof(payload))) {
        GATEWAY_LOG("[GATEWAY] cloud event payload too long\r\n");
        return;
    }

    (void)gateway_huawei_iot_publish_event(payload);
}
