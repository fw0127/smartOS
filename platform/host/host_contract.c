/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Contract-suite harness for the host virtual-time platform. Each test
 * starts just below the tick wrap so that wrap-around is always crossed.
 * There is no asynchronous context. */
#include <stdio.h>
#include "host_platform.h"
#include "sos_contract_test.h"

void sos_ct_setup(void)
{
#if SOS_PLATFORM_TICK_BITS == 64
    host_reset((uint64_t)0 - 100u);
#else
    host_reset(((uint64_t)1 << SOS_PLATFORM_TICK_BITS) - 100u);
#endif
}

void sos_ct_let_time_pass(sos_tick_t ticks) { host_advance((uint64_t)ticks); }
bool sos_ct_async_available(void)           { return false; }
void sos_ct_async_start(void (*fn)(void *arg), void *arg) { (void)fn; (void)arg; }
void sos_ct_async_join(void)                { }
void sos_ct_print(const char *line)         { printf("  %s\n", line); }

int main(void)
{
    int failed;
    printf("contract suite: host virtual-time platform, %d-bit ticks\n", SOS_PLATFORM_TICK_BITS);
    failed = sos_contract_run();
    printf("%d failed\n", failed);
    return (failed == 0) ? 0 : 1;
}
