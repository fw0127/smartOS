/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "thread_platform.h"

static pthread_mutex_t l_crit;
static pthread_mutex_t l_wait_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  l_wait_cv = PTHREAD_COND_INITIALIZER;
static bool            l_notified;
static uint64_t        l_t0;
static uint32_t        l_health;           /* scheduler thread only */

static uint64_t l_mono_us(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void thread_platform_init(void)
{
    pthread_mutexattr_t a;

    (void)pthread_mutexattr_init(&a);
    (void)pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    (void)pthread_mutex_init(&l_crit, &a);
    (void)pthread_mutexattr_destroy(&a);
    l_notified = false;
    l_health = 0u;
    l_t0 = l_mono_us();
}

uint32_t thread_platform_health_calls(void)
{
    return l_health;
}

sos_tick_t sos_platform_now(void)
{
    return (sos_tick_t)(l_mono_us() - l_t0);
}

void sos_platform_critical_enter(void)
{
    (void)pthread_mutex_lock(&l_crit);
}

void sos_platform_critical_exit(void)
{
    (void)pthread_mutex_unlock(&l_crit);
}

void sos_platform_wait_until(sos_tick_t deadline)
{
    sos_tick_t now = sos_platform_now();
    sos_tick_t dist = (sos_tick_t)(deadline - now);

    (void)pthread_mutex_lock(&l_wait_mx);
    if (!l_notified && (dist < ((sos_tick_t)1u << 62)))
    {
        struct timespec ts;
        uint64_t ns;

        (void)clock_gettime(CLOCK_REALTIME, &ts);   /* condvar clock */
        ns = (uint64_t)ts.tv_nsec + (uint64_t)dist * 1000u;
        ts.tv_sec += (time_t)(ns / 1000000000u);
        ts.tv_nsec = (long)(ns % 1000000000u);
        while (!l_notified)
        {
            if (pthread_cond_timedwait(&l_wait_cv, &l_wait_mx, &ts) != 0)
            {
                break;                              /* timeout */
            }
        }
    }
    l_notified = false;                             /* consume the sticky notify */
    (void)pthread_mutex_unlock(&l_wait_mx);
}

void sos_platform_notify(void)
{
    (void)pthread_mutex_lock(&l_wait_mx);
    l_notified = true;
    (void)pthread_cond_signal(&l_wait_cv);
    (void)pthread_mutex_unlock(&l_wait_mx);
}

void sos_platform_health_signal(void)
{
    l_health++;
}

void sos_platform_halt(uint32_t reason)
{
    fprintf(stderr, "thread platform: halt(%u)\n", (unsigned)reason);
    exit(2);
}
