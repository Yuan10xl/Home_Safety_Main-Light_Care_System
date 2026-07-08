#ifndef GATEWAY_APP_INTERFACE_H
#define GATEWAY_APP_INTERFACE_H

#include "care_event.h"
#include "gateway_cloud_report.h"

#ifdef __cplusplus
extern "C" {
#endif

void gateway_light_on_event(care_event_type_t event);
void gateway_cloud_upload_event(care_event_type_t event);
void gateway_cloud_upload_result(const gateway_cloud_status_t *status);
void gateway_cloud_upload_alarm(const gateway_cloud_status_t *status,
                                gateway_cloud_result_t result);
void gateway_app_notify_event(care_event_type_t event);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_APP_INTERFACE_H */
