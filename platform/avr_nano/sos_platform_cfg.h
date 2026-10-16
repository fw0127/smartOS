/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Arduino Nano (ATmega328P, 16 MHz), bare metal.
 *
 * Time source: Timer1, prescaler 64 -> 250 kHz (4 us per tick). The 16-bit
 * hardware count is extended by a software overflow counter to 32 bits, so
 * the tick wraps naturally at 2^32. Comparable range 2^31 / 250 kHz ~ 2.4 h;
 * a critical section may last up to one timer overflow (262 ms) without
 * losing time. */
#ifndef SOS_PLATFORM_CFG_H
#define SOS_PLATFORM_CFG_H

#define SOS_PLATFORM_TICK_BITS      32
#define SOS_PLATFORM_TICKS_PER_SEC  250000u

#endif
