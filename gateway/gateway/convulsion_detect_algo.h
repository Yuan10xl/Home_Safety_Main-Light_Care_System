#ifndef CONVULSION_DETECT_ALGO_H
#define CONVULSION_DETECT_ALGO_H

#include <stdint.h>

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t is_suspected_convulsion;
    uint8_t risk_context;
    uint8_t wave_feature;
    uint8_t normal_continuous_activity;
    uint16_t amplitude;
    uint8_t cross_count;
    uint8_t peak_count;
    uint8_t strong_count;
    uint8_t high_ratio;
    uint8_t max_high_run;
    const char *reason;
} convulsion_result_t;

void convulsion_detect_init(void);
convulsion_result_t convulsion_detect_update(const radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* CONVULSION_DETECT_ALGO_H */
