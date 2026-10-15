/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * smartOs platform capability contract.
 *
 * This is the kernel's only downward dependency. A platform adaptation
 * provides "sos_platform_cfg.h" (two constants) and implements the seven
 * functions below. See docs/05 section 1 for the required properties.
 */
#ifndef SOS_PLATFORM_H
#define SOS_PLATFORM_H

#include <stdint.h>
#include "sos_platform_cfg.h"

#if !defined(SOS_PLATFORM_TICK_BITS) || !defined(SOS_PLATFORM_TICKS_PER_SEC)
#error "sos_platform_cfg.h must define SOS_PLATFORM_TICK_BITS and SOS_PLATFORM_TICKS_PER_SEC"
#endif

#if SOS_PLATFORM_TICK_BITS == 16
typedef uint16_t sos_tick_t;
#elif SOS_PLATFORM_TICK_BITS == 32
typedef uint32_t sos_tick_t;
#elif SOS_PLATFORM_TICK_BITS == 64
typedef uint64_t sos_tick_t;
#else
#error "SOS_PLATFORM_TICK_BITS must be 16, 32 or 64"
#endif

#if (SOS_PLATFORM_TICKS_PER_SEC) == 0
#error "SOS_PLATFORM_TICKS_PER_SEC must be > 0"
#endif

/* Time source: monotonic, wraps naturally, non-blocking, callable anywhere. */
sos_tick_t sos_platform_now(void);

/* Critical section: not interrupted by asynchronous contexts; nestable. */
void sos_platform_critical_enter(void);
void sos_platform_critical_exit(void);

/* Wait until 'deadline' or until notified. May return early or at once.
 * A notify that arrives before the wait starts must make it return at once. */
void sos_platform_wait_until(sos_tick_t deadline);

/* End an ongoing (or the next) wait early. Callable from async contexts. */
void sos_platform_notify(void);

/* Declare "scheduling is healthy". May be empty. */
void sos_platform_health_signal(void);

/* Terminate. Must not return. */
void sos_platform_halt(uint32_t reason);

#endif /* SOS_PLATFORM_H */
