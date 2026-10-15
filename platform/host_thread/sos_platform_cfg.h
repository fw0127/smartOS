/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Host thread platform: real monotonic time, asynchronous contexts are
 * other POSIX threads. */
#ifndef SOS_PLATFORM_CFG_H
#define SOS_PLATFORM_CFG_H

#define SOS_PLATFORM_TICK_BITS      64
#define SOS_PLATFORM_TICKS_PER_SEC  1000000u    /* 1 tick = 1 us */

#endif
