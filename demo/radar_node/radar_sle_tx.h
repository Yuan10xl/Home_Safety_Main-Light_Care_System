#ifndef RADAR_SLE_TX_H
#define RADAR_SLE_TX_H

#include <stddef.h>
#include <stdint.h>

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*radar_sle_send_bytes_fn)(const uint8_t *data, size_t len);

int radar_sle_tx_init(void);
void radar_sle_tx_set_send_bytes(radar_sle_send_bytes_fn send_bytes);
int radar_sle_send_packet(const radar_light_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_SLE_TX_H */
