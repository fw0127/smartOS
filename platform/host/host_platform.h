/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Host virtual-time platform: deterministic, single-threaded.
 *
 * Time only moves when the test (or a task simulating its execution cost)
 * advances it, or when the kernel waits. Internally time is kept as a 64-bit
 * absolute value; sos_platform_now() returns it truncated to the tick width,
 * so wrap-around can be tested by starting near the top of the range.
 */
#ifndef HOST_PLATFORM_H
#define HOST_PLATFORM_H

#include <setjmp.h>
#include <stdbool.h>
#include "sos_platform.h"

void     host_reset(uint64_t start_abs);
uint64_t host_abs_now(void);
void     host_advance(uint64_t ticks);

int      host_critical_depth(void);
uint32_t host_wait_calls(void);
uint32_t host_health_calls(void);
bool     host_notify_pending(void);

/* sos_platform_halt longjmps to host_halt_jb when armed; otherwise abort(). */
extern jmp_buf host_halt_jb;
extern bool    host_halt_armed;
extern bool    host_halted;
extern uint32_t host_halt_reason;

#endif
