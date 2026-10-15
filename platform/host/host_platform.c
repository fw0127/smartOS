/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include "host_platform.h"

static uint64_t l_now;
static int      l_crit_depth;
static uint32_t l_wait_calls;
static uint32_t l_health_calls;
static bool     l_notified;

jmp_buf  host_halt_jb;
bool     host_halt_armed;
bool     host_halted;
uint32_t host_halt_reason;

void host_reset(uint64_t start_abs)
{
    l_now = start_abs;
    l_crit_depth = 0;
    l_wait_calls = 0u;
    l_health_calls = 0u;
    l_notified = false;
    host_halt_armed = false;
    host_halted = false;
    host_halt_reason = 0u;
}

uint64_t host_abs_now(void)          { return l_now; }
void     host_advance(uint64_t t)    { l_now += t; }
int      host_critical_depth(void)   { return l_crit_depth; }
uint32_t host_wait_calls(void)       { return l_wait_calls; }
uint32_t host_health_calls(void)     { return l_health_calls; }
bool     host_notify_pending(void)   { return l_notified; }

/* ---- contract ----------------------------------------------------------- */

sos_tick_t sos_platform_now(void)
{
    return (sos_tick_t)l_now;
}

void sos_platform_critical_enter(void)
{
    l_crit_depth++;
}

void sos_platform_critical_exit(void)
{
    if (l_crit_depth <= 0)
    {
        fprintf(stderr, "host: unbalanced critical_exit\n");
        abort();
    }
    l_crit_depth--;
}

void sos_platform_wait_until(sos_tick_t deadline)
{
    sos_tick_t now = (sos_tick_t)l_now;
    sos_tick_t dist = (sos_tick_t)(deadline - now);
    sos_tick_t half = (sos_tick_t)((sos_tick_t)1u << (SOS_PLATFORM_TICK_BITS - 1));

    l_wait_calls++;
    if (l_notified)
    {
        l_notified = false;             /* sticky notify: return at once */
        return;
    }
    if (dist < half)
    {
        l_now += (uint64_t)dist;        /* jump to the deadline */
    }
}

void sos_platform_notify(void)
{
    l_notified = true;
}

void sos_platform_health_signal(void)
{
    l_health_calls++;
}

void sos_platform_halt(uint32_t reason)
{
    host_halted = true;
    host_halt_reason = reason;
    if (host_halt_armed)
    {
        host_halt_armed = false;
        longjmp(host_halt_jb, 1);
    }
    fprintf(stderr, "host: halt(%u) with no handler armed\n", (unsigned)reason);
    abort();
}
