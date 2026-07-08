#ifndef GATEWAY_SLE_RX_H
#define GATEWAY_SLE_RX_H

#include <stddef.h>
#include <stdint.h>

#include "../common/radar_protocol/radar_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*gateway_radar_packet_callback_t)(const radar_light_packet_t *packet);

#define GATEWAY_SAFE_LIGHT_CMD_ON  1u
#define GATEWAY_SAFE_LIGHT_CMD_OFF 2u

int gateway_sle_rx_init(void);
void gateway_sle_register_packet_callback(gateway_radar_packet_callback_t callback);
void gateway_on_sle_data_received(const uint8_t *data, size_t len);
int gateway_sle_send_safe_lighting_cmd(uint8_t cmd, const char *reason);

#ifdef __cplusplus
}
#endif

#endif /* GATEWAY_SLE_RX_H */
