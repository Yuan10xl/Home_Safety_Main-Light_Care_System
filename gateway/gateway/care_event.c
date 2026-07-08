#include "care_event.h"

#include <stdio.h>

#ifndef GATEWAY_LOG
#define GATEWAY_LOG(...) printf(__VA_ARGS__)
#endif

const char *care_event_name(care_event_type_t event)
{
    switch (event) {
        case CARE_EVENT_NONE:
            return "NONE";
        case CARE_EVENT_SUSPECTED_FALL:
            return "SUSPECTED_FALL";
        case CARE_EVENT_SUSPECTED_CONVULSION:
            return "SUSPECTED_CONVULSION";
        case CARE_EVENT_RADAR_OFFLINE:
            return "RADAR_OFFLINE";
        case CARE_EVENT_RADAR_RECOVERED:
            return "RADAR_RECOVERED";
        default:
            return "UNKNOWN";
    }
}

void care_event_emit(care_event_type_t event)
{
    if (event == CARE_EVENT_NONE) {
        return;
    }

    GATEWAY_LOG("[GATEWAY] event=%s\r\n", care_event_name(event));
}
