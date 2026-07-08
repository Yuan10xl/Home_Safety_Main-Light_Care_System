#ifndef FALL_DETECT_ALGO_H
#define FALL_DETECT_ALGO_H

#include <stdint.h>

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t is_suspected_fall;
    uint8_t is_low_activity_observe;
    const char *reason;
} fall_result_t;

void fall_detect_init(void);
fall_result_t fall_detect_update(const radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* FALL_DETECT_ALGO_H */
