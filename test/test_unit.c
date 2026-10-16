/* Unit tests: time, bitmap, configuration check, error log. */
#include <string.h>
#include "test_util.h"
#include "../kernel/src/sos_kernel_pri.h"

/* ---- time ---------------------------------------------------------------- */

void test_time_reached_wraps(void)
{
    sos_tick_t top = (sos_tick_t)near_wrap(10u);
    sos_tick_t after = (sos_tick_t)(top + 20u);       /* wrapped past zero */

    CHECK(sos_i_reached(top, top));
    CHECK(sos_i_reached(after, top));
    CHECK(!sos_i_reached(top, after));
    CHECK(sos_i_reached((sos_tick_t)(top + 1u), top));
    CHECK(!sos_i_reached(top, (sos_tick_t)(top + 1u)));
}

void test_time_conversion(void)
{
    /* host platform: 1 tick = 1 us = 1 configuration unit */
    CHECK_EQ(sos_time_to_ticks(0u), 0u);
    CHECK_EQ(sos_time_to_ticks(1u), 1u);
    CHECK_EQ(sos_time_to_ticks(1500u), 1500u);
    CHECK_EQ(sos_ticks_to_time(2500u), 2500u);
}

void test_time_elapsed_across_wrap(void)
{
    sos_tick_t start;

    fresh(near_wrap(5u));
    start = sos_time_now();
    host_advance(10u);                                 /* crosses the wrap */
    CHECK_EQ(sos_time_since(start), 10u);
    CHECK(sos_time_elapsed(start, 10u));
    CHECK(!sos_time_elapsed(start, 11u));
}

/* ---- bitmap -------------------------------------------------------------- */

static uint8_t naive_lowest(uint32_t x)
{
    uint8_t i = 0u;
    while ((x & 1u) == 0u) { x >>= 1; i++; }
    return i;
}

void test_bitmap_lowest(void)
{
    uint32_t i;
    uint32_t seed = 12345u;
    int bad = 0;

    for (i = 0u; i < 32u; i++)
    {
        CHECK_EQ(sos_i_lowest_bit((uint32_t)1u << i), i);
        CHECK_EQ(sos_i_lowest_bit(0xFFFFFFFFu << i), i);
    }
    for (i = 0u; i < 100000u; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        if ((seed != 0u) && (sos_i_lowest_bit(seed) != naive_lowest(seed)))
        {
            bad++;
        }
    }
    CHECK_EQ(bad, 0);
}

/* ---- configuration check -------------------------------------------------- */

static tt_t g_c[40];

static sos_status_t init_with(sos_task_desc_t *d, uint8_t n)
{
    static sos_cfg_root_t root;
    root = troot(d, n);
    return sos_init(&root);
}

void test_cfg_valid(void)
{
    sos_task_desc_t d[3];

    fresh(0u);
    memset(g_c, 0, sizeof g_c);
    d[0] = tdesc("a", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    d[1] = tdesc("b", 5u, SOS_TRIG_EVENT | SOS_TRIG_PERIODIC, 2000u, 0u, 0x3u, &g_c[1]);
    d[2] = tdesc("bg", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_c[2]);
    CHECK_EQ(init_with(d, 3u), SOS_OK);
    CHECK_EQ(g_c[0].inits, 1u);
    CHECK_EQ(g_c[2].inits, 1u);
    CHECK_EQ(sos_error_count(), 0u);
}

void test_cfg_rejects(void)
{
    sos_task_desc_t d[2];
    sos_cfg_root_t root;

    fresh(0u);
    memset(g_c, 0, sizeof g_c);

    CHECK_EQ(sos_init(NULL), SOS_E_PARAM);

    d[0] = tdesc("a", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    root = troot(d, 1u);
    root.abi_version = (uint16_t)(SOS_ABI_VERSION + 1u);
    CHECK_EQ(sos_init(&root), SOS_E_CFG);                       /* ABI mismatch */

    CHECK_EQ(init_with(d, 0u), SOS_E_CFG);                      /* empty */

    d[0] = tdesc("a", 2u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    d[1] = tdesc("b", 2u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[1]);
    CHECK_EQ(init_with(d, 2u), SOS_E_CFG);                      /* duplicate prio */

    d[1].prio = 1u;
    CHECK_EQ(init_with(d, 2u), SOS_E_CFG);                      /* not ascending */

    d[0] = tdesc("bg", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_c[0]);
    d[1] = tdesc("a", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[1]);
    CHECK_EQ(init_with(d, 2u), SOS_E_CFG);                      /* fg after bg */

    d[0] = tdesc("a", 1u, SOS_TRIG_PERIODIC, 0u, 0u, 0u, &g_c[0]);
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);                      /* period 0 */

    d[0] = tdesc("a", 1u, SOS_TRIG_EVENT, 0u, 0u, 0u, &g_c[0]);
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);                      /* empty mask */

    d[0] = tdesc("a", 1u, SOS_TRIG_EVENT, 0u, 0u, SOS_EV_ALARM, &g_c[0]);
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);                      /* reserved bit */

    d[0] = tdesc("a", 1u, SOS_TRIG_BACKGROUND | SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);                      /* bad combination */

    d[0] = tdesc("a", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    d[0].run = NULL;
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);                      /* no run */

    CHECK(sos_error_count() >= 1u);
    CHECK_EQ(g_c[0].inits, 0u);                                 /* nothing started */
    CHECK_EQ(sos_poll(NULL), SOS_POLL_NOT_INIT);
}

void test_cfg_time_range(void)
{
    sos_task_desc_t d[1];
    sos_time_t limit = sos_ticks_to_time(SOS_TICK_LIMIT);

    fresh(0u);
    if (limit >= 0xFFFFFFF0u)
    {
        printf("    (skipped: every 32-bit configuration time fits this tick width)\n");
        return;
    }
    d[0] = tdesc("a", 1u, SOS_TRIG_PERIODIC, limit + 2u, 0u, 0u, &g_c[0]);
    CHECK_EQ(init_with(d, 1u), SOS_E_CFG);
    d[0].period = limit - 2u;
    CHECK_EQ(init_with(d, 1u), SOS_OK);
}

void test_cfg_fg_limit(void)
{
    sos_task_desc_t d[33];
    uint8_t i;

    fresh(0u);
    for (i = 0u; i < 33u; i++)
    {
        d[i] = tdesc("t", i, SOS_TRIG_EVENT, 0u, 0u, 1u, &g_c[i]);
    }
    CHECK_EQ(init_with(d, 32u), SOS_OK);
    CHECK_EQ(init_with(d, 33u), SOS_E_CFG);
}

/* ---- error log ------------------------------------------------------------ */

void test_errlog_ring(void)
{
    sos_error_entry_t e;
    sos_status_t i;

    fresh(0u);
    sos_i_error_reset();
    for (i = SOS_E_PARAM; i <= SOS_E_RESULT; i++)               /* 8 entries, depth 4 */
    {
        sos_i_error_log(1u, i);
    }
    CHECK_EQ(sos_error_count(), 8u);
    CHECK_EQ(sos_error_get(0u, &e), SOS_OK);
    CHECK_EQ(e.code, SOS_E_EMPTY);                              /* oldest kept: 5th logged */
    CHECK_EQ(sos_error_get(3u, &e), SOS_OK);
    CHECK_EQ(e.code, SOS_E_RESULT);
    CHECK_EQ(sos_error_get(4u, &e), SOS_E_PARAM);
}

/* Regression: background task ids may be >= 32 (outside the ready bitmap).
 * Stopping and restarting one must not shift past the bitmap width. */
void test_background_high_id(void)
{
    sos_task_desc_t d[33];
    static sos_cfg_root_t root;
    sos_task_stats_t st;
    uint8_t i;

    fresh(0u);
    memset(g_c, 0, sizeof g_c);
    for (i = 0u; i < 32u; i++)
    {
        d[i] = tdesc("t", i, SOS_TRIG_EVENT, 0u, 0u, 1u, &g_c[i]);
    }
    d[32] = tdesc("bg", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_c[32]);
    root = troot(d, 33u);
    CHECK_EQ(sos_init(&root), SOS_OK);
    CHECK_EQ(sos_poll(NULL), SOS_POLL_RAN);
    CHECK_EQ(g_c[32].runs, 1u);
    CHECK_EQ(sos_task_get_stats(32u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_READY);
    CHECK_EQ(sos_task_stop(32u), SOS_OK);
    CHECK_EQ(sos_task_restart(32u), SOS_OK);
    CHECK_EQ(sos_poll(NULL), SOS_POLL_RAN);
    CHECK_EQ(g_c[32].runs, 2u);
    CHECK_EQ(sos_event_set(31u, 1u), SOS_OK);
    CHECK_EQ(sos_poll(NULL), SOS_POLL_RAN);
    CHECK_EQ(g_c[31].runs, 1u);
}

#if !SOS_CFG_RING_ENABLE
/* Rings compiled out: a configuration that declares rings is rejected. */
void test_rings_disabled(void)
{
    static uint32_t store[4];
    static sos_ring_desc_t rd;
    static sos_cfg_root_t root;
    sos_task_desc_t d[1];

    fresh(0u);
    d[0] = tdesc("t", 0u, SOS_TRIG_EVENT, 0u, 0u, 1u, &g_c[0]);
    memset(&rd, 0, sizeof rd);
    rd.storage = store;
    rd.elem_size = 4u;
    rd.capacity = 4u;
    rd.producer = SOS_TASK_NONE;
    rd.consumer = SOS_TASK_NONE;
    root = troot(d, 1u);
    root.rings = &rd;
    root.ring_count = 1u;
    CHECK_EQ(sos_init(&root), SOS_E_CFG);
    root.ring_count = 0u;
    CHECK_EQ(sos_init(&root), SOS_OK);
}
#endif

#if !SOS_CFG_SUPERVISE_ENABLE
/* Supervision compiled out: alive windows are rejected (they would silently
 * not be checked), budgets are accepted but ignored, no health signal. */
void test_supervise_disabled(void)
{
    sos_task_desc_t d[1];
    static sos_cfg_root_t root;
    sos_task_stats_t st;

    fresh(0u);
    memset(g_c, 0, sizeof g_c);
    d[0] = tdesc("t", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_c[0]);
    d[0].alive_window = 5000u;
    root = troot(d, 1u);
    CHECK_EQ(sos_init(&root), SOS_E_CFG);

    d[0].alive_window = 0u;
    d[0].budget = 10u;
    g_c[0].cost = 500u;
    CHECK_EQ(sos_init(&root), SOS_OK);
    run_for(20000u);
    CHECK_EQ(g_c[0].runs, 20u);
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.overrun_cnt, 0u);
    CHECK_EQ(host_health_calls(), 0u);
    CHECK_EQ(host_wait_calls(), 19u);                 /* only between the 20 releases: no supervision wake-ups */
}
#endif

