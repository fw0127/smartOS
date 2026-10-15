/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Host virtual-time platform. The tick width can be chosen at build time so
 * the same tests exercise 16-, 32- and 64-bit time sources. */
#ifndef SOS_PLATFORM_CFG_H
#define SOS_PLATFORM_CFG_H

#ifndef SOS_PLATFORM_TICK_BITS
#define SOS_PLATFORM_TICK_BITS      32
#endif

#define SOS_PLATFORM_TICKS_PER_SEC  1000000u    /* 1 tick = 1 us */

#endif
