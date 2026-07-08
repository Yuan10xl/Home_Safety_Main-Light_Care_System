#ifndef RADAR_LAMP_SWITCH_H
#define RADAR_LAMP_SWITCH_H

#include <stdbool.h>
#include <stdint.h>
#include "errcode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADAR_LAMP_SAFE_CMD_ON  1u
#define RADAR_LAMP_SAFE_CMD_OFF 2u

/*
 * Radar-board lamp control.
 *
 * Hardware follows the lamp-control reference project:
 * - Lamp MOSFET/PWM input: D10 / GPIO7.
 * - Physical switch input: A11 / GPIO11, internal pull-up, switch to GND.
 *
 * Normal mode: physical switch controls full ON/OFF.
 * Safe-lighting mode: gateway command fades lamp to low-brightness PWM.
 * Local exit: toggle the switch away from its entry state and then back.
 */
errcode_t radar_lamp_switch_start(void);
bool radar_lamp_switch_is_on(void);
errcode_t radar_lamp_safe_lighting_request(uint8_t cmd);

#ifdef __cplusplus
}
#endif

#endif
