#ifndef CARE_EVENT_H
#define CARE_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CARE_EVENT_NONE = 0,
    CARE_EVENT_SUSPECTED_FALL,
    CARE_EVENT_SUSPECTED_CONVULSION,
    CARE_EVENT_RADAR_OFFLINE,
    CARE_EVENT_RADAR_RECOVERED
} care_event_type_t;

const char *care_event_name(care_event_type_t event);
void care_event_emit(care_event_type_t event);

#ifdef __cplusplus
}
#endif

#endif /* CARE_EVENT_H */
