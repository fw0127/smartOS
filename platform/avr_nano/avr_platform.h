/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Arduino Nano platform adaptation: board-level helpers outside the
 * contract (initialisation, polled serial output, the on-board LED). */
#ifndef AVR_PLATFORM_H
#define AVR_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "sos_platform.h"

/* Start Timer1, the UART (115200 8N1) and the LED pin, then enable
 * interrupts. With use_watchdog, the watchdog runs with a ~2 s timeout and
 * sos_platform_health_signal() serves it. */
void avr_platform_init(bool use_watchdog);

/* Reset cause latched at start-up (MCUSR: PORF, EXTRF, BORF, WDRF bits). */
uint8_t avr_reset_cause(void);

/* Polled serial output; safe with interrupts disabled. */
void avr_puts(const char *s);          /* no newline added */
void avr_put_u32(uint32_t v);          /* decimal */
void avr_put_hex32(uint32_t v);
void avr_newline(void);

/* On-board LED (D13 / PB5). */
void avr_led(bool on);
void avr_led_toggle(void);

#endif
