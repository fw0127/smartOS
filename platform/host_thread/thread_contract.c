#define _POSIX_C_SOURCE 200809L
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Contract-suite harness for the host thread platform: real time,
 * a POSIX thread as the asynchronous context. */
#include <pthread.h>
#include <stdio.h>
#include "thread_platform.h"
#include "sos_contract_test.h"

static pthread_t l_th;
static void (*l_fn)(void *);
static void *l_arg;

static void *l_entry(void *unused)
{
    (void)unused;
    l_fn(l_arg);
    return NULL;
}

void sos_ct_setup(void) { }

void sos_ct_let_time_pass(sos_tick_t ticks)
{
    sos_tick_t t0 = sos_platform_now();
    while ((sos_tick_t)(sos_platform_now() - t0) < ticks) { }
}

bool sos_ct_async_available(void) { return true; }

void sos_ct_async_start(void (*fn)(void *arg), void *arg)
{
    l_fn = fn;
    l_arg = arg;
    (void)pthread_create(&l_th, NULL, l_entry, NULL);
}

void sos_ct_async_join(void)        { (void)pthread_join(l_th, NULL); }
void sos_ct_print(const char *line) { printf("  %s\n", line); }

int main(void)
{
    int failed;
    thread_platform_init();
    printf("contract suite: host thread platform (real time)\n");
    failed = sos_contract_run();
    printf("%d failed\n", failed);
    return (failed == 0) ? 0 : 1;
}
