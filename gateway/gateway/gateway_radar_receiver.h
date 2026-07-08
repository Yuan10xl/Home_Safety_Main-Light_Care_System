#ifndef GATEWAY_RADAR_RECEIVER_H
#define GATEWAY_RADAR_RECEIVER_H

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

void gateway_radar_receiver_init(void);
void gateway_on_radar_packet(const radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_RADAR_RECEIVER_H */
