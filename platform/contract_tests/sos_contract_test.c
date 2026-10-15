/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Platform contract conformance checks. Uses no C library beyond the
 * harness's print function, so it can run on a target as well.
 */
#include <stddef.h>
#include "sos_contract_test.h"

#define L_HALF   ((sos_tick_t)((sos_tick_t)1u << (SOS_PLATFORM_TICK_BITS - 1)))

static int  l_fail;
static int  l_skip;
static const char *l_test;

static bool l_reached(sos_tick_t now, sos_tick_t t)
{
    return (sos_tick_t)(now - t) < L_HALF;
}

static void l_line(const char *a, const char *b)
{
    char buf[96];
    unsigned i = 0u;
    while ((*a != '\0') && (i < 60u)) { buf[i++] = *a++; }
    while (i < 44u) { buf[i++] = ' '; }
    while ((*b != '\0') && (i < 94u)) { buf[i++] = *b++; }
    buf[i] = '\0';
    sos_ct_print(buf);
}

#define CT_CHECK(c) do { if (!(c)) { l_fail++; l_line(l_test, "FAIL: " #c); return; } } while (0)

/* A tick distance that is "long" for this platform but well inside the
 * comparable range: min(1 s, half range / 4). */
static sos_tick_t l_long(void)
{
    sos_tick_t q = (sos_tick_t)(L_HALF / 4u);
    return ((sos_tick_t)(SOS_PLATFORM_TICKS_PER_SEC) < q)
           ? (sos_tick_t)(SOS_PLATFORM_TICKS_PER_SEC) : q;
}

/* ---- checks ------------------------------------------------------------ */

static void ct_constants(void)
{
    CT_CHECK((SOS_PLATFORM_TICK_BITS == 16) || (SOS_PLATFORM_TICK_BITS == 32)
             || (SOS_PLATFORM_TICK_BITS == 64));
    CT_CHECK(sizeof(sos_tick_t) * 8u == (unsigned)SOS_PLATFORM_TICK_BITS);
    CT_CHECK((SOS_PLATFORM_TICKS_PER_SEC) > 0u);
}

/* Time never runs backwards, also across the natural wrap. */
static void ct_time_monotonic(void)
{
    sos_tick_t prev = sos_platform_now();
    unsigned long i;

    for (i = 0u; i < 200000u; i++)
    {
        sos_tick_t now = sos_platform_now();
        CT_CHECK(l_reached(now, prev));
        prev = now;
        if ((i % 1000u) == 0u)
        {
            sos_ct_let_time_pass(1u);
        }
    }
}

/* Time advances by at least what the harness lets pass. */
static void ct_time_progress(void)
{
    sos_tick_t t0 = sos_platform_now();
    sos_tick_t d = (sos_tick_t)(l_long() / 100u + 1u);

    sos_ct_let_time_pass(d);
    CT_CHECK((sos_tick_t)(sos_platform_now() - t0) >= d);
}

/* Critical sections nest. */
static void ct_critical_nesting(void)
{
    sos_platform_critical_enter();
    sos_platform_critical_enter();
    sos_platform_critical_exit();
    sos_platform_critical_exit();
    sos_platform_critical_enter();
    sos_platform_critical_exit();
    CT_CHECK(true);
}

/* An asynchronous context cannot run inside a critical section. */
static volatile unsigned long l_counter;
static volatile bool l_stop;

/* both variables are only touched inside critical sections */
static unsigned long l_read_counter(void)
{
    unsigned long c;
    sos_platform_critical_enter();
    c = l_counter;
    sos_platform_critical_exit();
    return c;
}

static void l_async_counter(void *arg)
{
    bool stop = false;
    (void)arg;
    while (!stop)
    {
        sos_platform_critical_enter();
        l_counter++;
        (void)sos_platform_now();               /* now is callable from async */
        stop = l_stop;
        sos_platform_critical_exit();
    }
}

static void ct_critical_excludes_async(void)
{
    unsigned long c1, c2, c3;
    sos_tick_t d = (sos_tick_t)(l_long() / 200u + 1u);

    if (!sos_ct_async_available())
    {
        l_skip++;
        l_line(l_test, "skipped (no async context)");
        return;
    }
    l_counter = 0u;
    l_stop = false;
    sos_ct_async_start(l_async_counter, NULL);
    while (l_read_counter() < 1000u) { }        /* the async side is running */

    sos_platform_critical_enter();
    sos_platform_critical_enter();              /* nested */
    c1 = l_counter;
    sos_ct_let_time_pass(d);
    sos_platform_critical_exit();
    c2 = l_counter;                              /* still inside the outer section */
    sos_ct_let_time_pass(d);
    sos_platform_critical_exit();
    sos_ct_let_time_pass(d);
    c3 = l_read_counter();

    sos_platform_critical_enter();
    l_stop = true;
    sos_platform_critical_exit();
    sos_ct_async_join();
    CT_CHECK(c1 == c2);                          /* excluded while held */
    CT_CHECK(c3 > c2);                           /* runs again after exit */
}

/* wait_until may return early, but repeated waits reach the deadline. */
static void ct_wait_reaches_deadline(void)
{
    sos_tick_t deadline = (sos_tick_t)(sos_platform_now() + l_long() / 50u + 1u);
    unsigned long n = 0u;

    while (!l_reached(sos_platform_now(), deadline))
    {
        sos_platform_wait_until(deadline);
        n++;
        CT_CHECK(n < 1000000u);                  /* returns early, but not forever */
    }
    CT_CHECK(l_reached(sos_platform_now(), deadline));
}

/* A deadline in the past returns at once. */
static void ct_wait_past_deadline(void)
{
    sos_tick_t t0 = sos_platform_now();
    sos_platform_wait_until((sos_tick_t)(t0 - 1u));
    CT_CHECK((sos_tick_t)(sos_platform_now() - t0) < l_long() / 2u);
}

/* Sticky notify: a notify before the wait makes the next wait return at once. */
static void ct_notify_sticky(void)
{
    sos_tick_t t0;

    sos_platform_notify();
    t0 = sos_platform_now();
    sos_platform_wait_until((sos_tick_t)(t0 + l_long()));
    CT_CHECK((sos_tick_t)(sos_platform_now() - t0) < l_long() / 2u);
}

/* Notify from an asynchronous context ends an ongoing wait. */
static void l_async_notify(void *arg)
{
    (void)arg;
    sos_ct_let_time_pass((sos_tick_t)(l_long() / 20u + 1u));
    sos_platform_notify();
}

static void ct_notify_async_ends_wait(void)
{
    sos_tick_t t0;

    if (!sos_ct_async_available())
    {
        l_skip++;
        l_line(l_test, "skipped (no async context)");
        return;
    }
    t0 = sos_platform_now();
    sos_ct_async_start(l_async_notify, NULL);
    sos_platform_wait_until((sos_tick_t)(t0 + l_long()));
    sos_ct_async_join();
    CT_CHECK((sos_tick_t)(sos_platform_now() - t0) < l_long() / 2u);
}

/* The health signal does not block. */
static void ct_health_nonblocking(void)
{
    sos_tick_t t0 = sos_platform_now();
    unsigned i;

    for (i = 0u; i < 100u; i++)
    {
        sos_platform_health_signal();
    }
    CT_CHECK((sos_tick_t)(sos_platform_now() - t0) < l_long() / 10u);
}

/* ---- runner -------------------------------------------------------------- */

typedef struct { const char *name; void (*fn)(void); } l_case_t;

static const l_case_t l_cases[] =
{
    { "constants",                 ct_constants },
    { "time: monotonic",           ct_time_monotonic },
    { "time: progress",            ct_time_progress },
    { "critical: nesting",         ct_critical_nesting },
    { "critical: excludes async",  ct_critical_excludes_async },
    { "wait: reaches deadline",    ct_wait_reaches_deadline },
    { "wait: past deadline",       ct_wait_past_deadline },
    { "notify: sticky",            ct_notify_sticky },
    { "notify: async ends wait",   ct_notify_async_ends_wait },
    { "health: non-blocking",      ct_health_nonblocking },
};

int sos_contract_run(void)
{
    unsigned i;

    l_fail = 0;
    l_skip = 0;
    for (i = 0u; i < sizeof l_cases / sizeof l_cases[0]; i++)
    {
        int before_fail = l_fail;
        int before_skip = l_skip;
        l_test = l_cases[i].name;
        sos_ct_setup();
        l_cases[i].fn();
        if ((l_fail == before_fail) && (l_skip == before_skip))
        {
            l_line(l_test, "ok");
        }
    }
    return l_fail;
}
