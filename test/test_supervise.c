/* Supervision tests: budgets, alive windows, health signal (docs/03 section 8).
 * SOS_CFG_HEALTH_MIN_INTERVAL is the default 10 ms. */
#include <string.h>
#include "test_util.h"

#if SOS_CFG_SUPERVISE_ENABLE

static tt_t g_t[3];
static sos_task_desc_t g_d[3];
static sos_cfg_root_t g_root;
static int g_overruns;
static sos_tick_t g_overrun_dt;

static void on_overrun(sos_task_id_t id, sos_tick_t dt)
{
    (void)id;
    g_overruns++;
    g_overrun_dt = dt;
}

static const sos_hooks_t g_hooks = { on_overrun, NULL, NULL, NULL, NULL };

static void reset(void)
{
    fresh(0u);
    memset(g_t, 0, sizeof g_t);
    g_overruns = 0;
    g_overrun_dt = 0u;
}

static sos_status_t start(uint8_t n)
{
    g_root = troot(g_d, n);
    g_root.hooks = &g_hooks;
    return sos_init(&g_root);
}

static unsigned count_errors(sos_status_t code)
{
    unsigned n = 0u;
    uint32_t i;
    sos_error_entry_t e;
    for (i = 0u; sos_error_get(i, &e) == SOS_OK; i++)
    {
        if (e.code == code) { n++; }
    }
    return n;
}

void test_budget_overrun(void)
{
    sos_task_stats_t st;
    static const uint64_t costs[] = { 80u, 150u };

    reset();
    g_d[0] = tdesc("b", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].budget = 100u;
    g_t[0].cost = costs[0];
    CHECK_EQ(start(1u), SOS_OK);
    run_for(1000u);
    g_t[0].cost = costs[1];
    run_for(1000u);

    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.overrun_cnt, 1u);
    CHECK_EQ(g_overruns, 1);
    CHECK_EQ(g_overrun_dt, 150u);
    CHECK_EQ(count_errors(SOS_E_OVERRUN), 0u);         /* default: not an error */
}

void test_precision_warning(void)
{
    reset();
    g_d[0] = tdesc("b", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].budget = 5u;                               /* 5 ticks: resolution > budget/10 */
    CHECK_EQ(start(1u), SOS_OK);
    CHECK_EQ(count_errors(SOS_W_PRECISION), 1u);
}

/* While idle the kernel still wakes for supervision and keeps signalling. */
void test_health_while_idle(void)
{
    reset();
    g_d[0] = tdesc("slow", 0u, SOS_TRIG_PERIODIC, 15000u, 0u, 0u, &g_t[0]); /* < 16-bit limit */
    CHECK_EQ(start(1u), SOS_OK);
    run_for(100000u);
    CHECK_EQ(host_health_calls(), 10u);               /* at 0, 10, ..., 90 ms */
    CHECK_EQ(g_t[0].runs, 7u);                        /* 0, 15, ..., 90 ms */
    CHECK(host_wait_calls() > 10u);                   /* it really slept in between */
}

/* Overload: a busy high-priority task starves the heartbeat. Its alive
 * window expires once (logged once), health stops, and resumes after the
 * heartbeat runs again. */
void test_alive_starvation_stops_health(void)
{
    uint32_t h1, h2;

    reset();
    g_d[0] = tdesc("busy", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_t[0].dflt = SOS_RUN_AGAIN;
    g_t[0].cost = 100u;
    g_d[1] = tdesc("hb", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[1]);
    g_d[1].alive_window = 5000u;
    g_d[1].on_error = SOS_ON_ERR_IGNORE;
    CHECK_EQ(start(2u), SOS_OK);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(30000u);
    h1 = host_health_calls();
    CHECK_EQ(g_t[1].runs, 0u);
    CHECK_EQ(h1, 1u);                                 /* only the check at t = 0 */
    CHECK_EQ(count_errors(SOS_E_ALIVE), 1u);          /* not repeated while failing */

    CHECK_EQ(sos_task_stop(0u), SOS_OK);              /* load goes away */
    run_for(30000u);
    h2 = host_health_calls();
    CHECK(g_t[1].runs > 0u);
    CHECK(h2 >= h1 + 2u);
    CHECK_EQ(count_errors(SOS_E_ALIVE), 1u);
}

/* An event task that never gets events fails its alive window; RESTART
 * re-arms it until max_restart is exceeded. */
void test_alive_restart_then_fatal(void)
{
    uint32_t reason;

    reset();
    g_d[0] = tdesc("ev", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].alive_window = 5000u;
    g_d[0].on_error = SOS_ON_ERR_RESTART;
    g_d[0].max_restart = 3u;
    CHECK_EQ(start(1u), SOS_OK);

    EXPECT_HALT(run_for(100000u), reason);
    CHECK_EQ(reason, SOS_E_RESTART_LIMIT);
    CHECK_EQ(g_t[0].inits, 4u);                       /* initial + 3 restarts */
    CHECK_EQ(g_t[0].dones, 4u);
}

/* STOP removes the failing task from supervision; health resumes. */
void test_alive_stop(void)
{
    sos_task_stats_t st;
    uint32_t h1;

    reset();
    g_d[0] = tdesc("ev", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].alive_window = 5000u;
    g_d[0].on_error = SOS_ON_ERR_STOP;
    CHECK_EQ(start(1u), SOS_OK);

    run_for(15000u);                                  /* fails at the 10 ms check */
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_STOPPED);
    h1 = host_health_calls();
    run_for(30000u);
    CHECK(host_health_calls() >= h1 + 3u);
}

#endif /* SOS_CFG_SUPERVISE_ENABLE */
