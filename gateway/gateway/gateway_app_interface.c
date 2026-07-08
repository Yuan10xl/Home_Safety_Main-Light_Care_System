#include "gateway_app_interface.h"

#include <stdio.h>

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

void gateway_light_on_event(care_event_type_t event)
{
    /* TODO: Connect gateway-side light/main-control handling if needed. */
    GATEWAY_LOG("[GATEWAY] light interface reserved event=%s\r\n",
                care_event_name(event));
}

void gateway_cloud_upload_event(care_event_type_t event)
{
    /* Event-only compatibility hook. Prefer gateway_cloud_upload_result(). */
    GATEWAY_LOG("[GATEWAY] cloud interface reserved event=%s\r\n",
                care_event_name(event));
}

void gateway_cloud_upload_result(const gateway_cloud_status_t *status)
{
    gateway_cloud_report_status(status);
}

void gateway_cloud_upload_alarm(const gateway_cloud_status_t *status,
                                gateway_cloud_result_t result)
{
    gateway_cloud_report_event(status, result);
}

void gateway_app_notify_event(care_event_type_t event)
{
    /* TODO: Reserved for the App notification teammate. */
    GATEWAY_LOG("[GATEWAY] app interface reserved event=%s\r\n",
                care_event_name(event));
}
