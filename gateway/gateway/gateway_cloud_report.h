#ifndef GATEWAY_CLOUD_REPORT_H
#define GATEWAY_CLOUD_REPORT_H

#include "convulsion_detect_algo.h"
#include "fall_detect_algo.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GATEWAY_CLOUD_RESULT_NORMAL = 0,
    GATEWAY_CLOUD_RESULT_LOW_ACTIVITY_OBSERVE,
    GATEWAY_CLOUD_RESULT_SUSPECTED_FALL,
    GATEWAY_CLOUD_RESULT_SUSPECTED_ABNORMAL_ACTIVITY_WAVE
} gateway_cloud_result_t;

typedef struct {
    uint8_t motion_level;
    uint16_t static_time;
    uint8_t fall_result;
    uint8_t convulsion_result;
    uint8_t low_observe;
    uint8_t radar_online;
    gateway_cloud_result_t final_result;
} gateway_cloud_status_t;

gateway_cloud_result_t gateway_cloud_select_result(
    const fall_result_t *fall_result,
    const convulsion_result_t *convulsion_result);

const char *gateway_cloud_result_name(gateway_cloud_result_t result);
void gateway_cloud_report_status(const gateway_cloud_status_t *status);
void gateway_cloud_report_event(const gateway_cloud_status_t *status,
                                gateway_cloud_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_CLOUD_REPORT_H */
