/* Error policies, hooks and the fatal path (docs/03 section 9). */
#include <string.h>
#include "test_util.h"

static tt_t g_t[3];
static sos_task_desc_t g_d[3];
static sos_cfg_root_t g_root;

static int g_hook_err;
static sos_status_t g_hook_err_code;
static int g_hook_fatal;
static sos_status_t g_hook_fatal_code;

static void on_task_error(sos_task_id_t id, sos_status_t code)
{
    (void)id;
    g_hook_err++;
    g_hook_err_code = code;
}

static void on_fatal(sos_status_t code)
{
    g_hook_fatal++;
    g_hook_fatal_code = code;
}

static const sos_hooks_t g_hooks = { NULL, on_task_error, on_fatal, NULL, NULL };

static void reset(void)
{
    fresh(0u);
    memset(g_t, 0, sizeof g_t);
    g_hook_err = 0;
    g_hook_fatal = 0;
    g_hook_err_code = SOS_OK;
    g_hook_fatal_code = SOS_OK;
}

static sos_status_t start(uint8_t n)
{
    g_root = troot(g_d, n);
    g_root.hooks = &g_hooks;
    return sos_init(&g_root);
}

void test_policy_restart_limit(void)
{
    uint32_t reason;

    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_RESTART;
    g_d[0].max_restart = 2u;
    g_t[0].dflt = SOS_RUN_ERR;
    CHECK_EQ(start(1u), SOS_OK);

    EXPECT_HALT(run_for(10000u), reason);
    CHECK_EQ(reason, SOS_E_RESTART_LIMIT);
    CHECK_EQ(g_t[0].runs, 3u);
    CHECK_EQ(g_t[0].inits, 3u);           /* initial + 2 restarts */
    CHECK_EQ(g_t[0].dones, 3u);           /* before each restart attempt */
    CHECK_EQ(g_hook_err, 3);
    CHECK_EQ(g_hook_fatal, 1);
    CHECK_EQ(g_hook_fatal_code, SOS_E_RESTART_LIMIT);
}

void test_policy_restart_counter_resets(void)
{
    static const sos_run_result_t script[] =
        { SOS_RUN_ERR, SOS_RUN_OK, SOS_RUN_ERR, SOS_RUN_OK, SOS_RUN_ERR, SOS_RUN_OK };
    uint32_t reason;

    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_RESTART;
    g_d[0].max_restart = 1u;
    g_t[0].script = script;
    g_t[0].script_len = 6u;
    CHECK_EQ(start(1u), SOS_OK);

    EXPECT_HALT(run_for(10000u), reason);
    CHECK_EQ(reason, 0xFFFFFFFFu);        /* no halt: each success clears the count */
    CHECK_EQ(g_t[0].inits, 4u);
}

void test_policy_stop(void)
{
    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_STOP;
    g_d[1] = tdesc("ok", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[1]);
    g_t[0].dflt = SOS_RUN_ERR;
    CHECK_EQ(start(2u), SOS_OK);

    run_for(5000u);
    CHECK_EQ(g_t[0].runs, 1u);
    CHECK_EQ(g_t[0].dones, 1u);
    CHECK_EQ(g_t[1].runs, 5u);
    CHECK_EQ(g_hook_err_code, SOS_E_TASK);
}

void test_policy_ignore_no_hot_loop(void)
{
    sos_error_entry_t e;

    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_IGNORE;
    g_t[0].dflt = SOS_RUN_ERR;
    CHECK_EQ(start(1u), SOS_OK);

    run_for(5000u);
    CHECK_EQ(g_t[0].runs, 5u);
    CHECK_EQ(sos_error_count(), 5u);
    CHECK_EQ(sos_error_get(0u, &e), SOS_OK);
    CHECK_EQ(e.task, 0u);
    CHECK_EQ(e.code, SOS_E_TASK);
}

void test_policy_fatal(void)
{
    uint32_t reason;

    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_t[0].dflt = SOS_RUN_ERR;                           /* tdesc default: FATAL */
    CHECK_EQ(start(1u), SOS_OK);

    EXPECT_HALT(run_for(5000u), reason);
    CHECK_EQ(reason, SOS_E_TASK);
    CHECK_EQ(g_hook_fatal, 1);
}

void test_unknown_result(void)
{
    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_IGNORE;
    g_t[0].dflt = (sos_run_result_t)7;
    CHECK_EQ(start(1u), SOS_OK);

    run_for(1000u);
    CHECK_EQ(g_hook_err_code, SOS_E_RESULT);
}

void test_init_failure_policies(void)
{
    sos_task_stats_t st;
    uint32_t reason;

    /* RESTART: retried within max_restart, then runs normally */
    reset();
    g_d[0] = tdesc("r", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_RESTART;
    g_d[0].max_restart = 3u;
    g_t[0].init_failures = 2u;
    /* STOP: never runs */
    g_d[1] = tdesc("s", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[1]);
    g_d[1].on_error = SOS_ON_ERR_STOP;
    g_t[1].init_failures = 1u;
    CHECK_EQ(start(2u), SOS_OK);
    run_for(3000u);

    CHECK_EQ(g_t[0].inits, 3u);
    CHECK_EQ(g_t[0].dones, 0u);           /* failed inits are not undone */
    CHECK_EQ(g_t[0].runs, 3u);
    CHECK_EQ(g_t[1].runs, 0u);
    CHECK_EQ(sos_task_get_stats(1u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_STOPPED);
    CHECK_EQ(g_hook_err, 3);

    /* FATAL: sos_init itself halts */
    reset();
    g_d[0] = tdesc("f", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_t[0].init_failures = 1u;
    EXPECT_HALT((void)start(1u), reason);
    CHECK_EQ(reason, SOS_E_INIT);
}

static void restart_self_first(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 1u)
    {
        CHECK_EQ(sos_task_restart(sos_task_self()), SOS_OK);
    }
}

void test_restart_request(void)
{
    sos_task_stats_t st;

    reset();
    g_d[0] = tdesc("r", 0u, SOS_TRIG_PERIODIC, 1000u, 500u, 0u, &g_t[0]);
    g_t[0].action = restart_self_first;
    CHECK_EQ(start(1u), SOS_OK);

    run_for(3000u);
    CHECK_EQ(g_t[0].inits, 2u);
    CHECK_EQ(g_t[0].dones, 1u);
    CHECK_EQ(g_trace[0].at, 500u);
    CHECK_EQ(g_trace[1].at, 1000u);       /* re-armed: now + offset */
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.restart_cnt, 0u);         /* a requested restart is not a failure */
}

/* ---- D16: lifecycle identity and deferred cross-task control ---------------- */

static sos_task_id_t g_init_self[3];
static sos_task_id_t g_done_self[3];
static int g_a_in_step;                 /* 1 while task A's step is running */
static int g_b_cb_inside_a;             /* B's init/done ran inside A's step */

static sos_status_t id_init(void *ctx)
{
    tt_t *t = (tt_t *)ctx;
    sos_task_id_t me = sos_task_self();
    t->inits++;
    if (me < 3u)
    {
        g_init_self[me] = me;
        if ((me == 1u) && (t->inits == 2u))
        {
            CHECK_EQ(sos_task_set_alarm(1000u), SOS_OK);   /* must land on B, not on A */
        }
    }
    if (g_a_in_step != 0) { g_b_cb_inside_a = 1; }
    return SOS_OK;
}

static void id_done(void *ctx)
{
    tt_t *t = (tt_t *)ctx;
    sos_task_id_t me = sos_task_self();
    t->dones++;
    if (me < 3u) { g_done_self[me] = me; }
    if (g_a_in_step != 0) { g_b_cb_inside_a = 1; }
}

static void a_controls_b(tt_t *self, sos_events_t ev)
{
    (void)ev;
    g_a_in_step = 1;
    if (self->runs == 1u)
    {
        CHECK_EQ(sos_task_restart(1u), SOS_OK);
        CHECK_EQ(g_t[1].dones, 0u);                     /* not yet: deferred */
    }
    else if (self->runs == 2u)
    {
        CHECK_EQ(sos_task_stop(2u), SOS_OK);
        CHECK_EQ(g_t[2].dones, 0u);
    }
    g_a_in_step = 0;
}

void test_lifecycle_identity_and_deferral(void)
{
    sos_task_stats_t st;
    uint8_t i;

    reset();
    memset(g_init_self, 0xFF, sizeof g_init_self);
    memset(g_done_self, 0xFF, sizeof g_done_self);
    g_a_in_step = 0;
    g_b_cb_inside_a = 0;
    g_d[0] = tdesc("A", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_t[0].action = a_controls_b;
    g_d[1] = tdesc("B", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[1]);
    g_d[2] = tdesc("C", 2u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[2]);
    for (i = 0u; i < 3u; i++)
    {
        g_d[i].init = id_init;
        g_d[i].done = id_done;
    }
    CHECK_EQ(start(3u), SOS_OK);
    for (i = 0u; i < 3u; i++)
    {
        CHECK_EQ(g_init_self[i], i);                    /* startup init knows itself */
    }

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);         /* A restarts B */
    run_for(100u);
    CHECK_EQ(g_t[1].inits, 2u);
    CHECK_EQ(g_done_self[1], 1u);
    CHECK_EQ(g_b_cb_inside_a, 0);                       /* never inside A's step */

    run_for(2000u);                                     /* B's alarm set in its init */
    CHECK_EQ(g_t[1].runs, 1u);
    CHECK_EQ(g_t[1].last_ev, SOS_EV_ALARM);
    CHECK_EQ(g_t[0].runs, 1u);                          /* A got no alarm */

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);         /* A stops C */
    run_for(100u);
    CHECK_EQ(g_t[2].dones, 1u);
    CHECK_EQ(g_done_self[2], 2u);
    CHECK_EQ(g_b_cb_inside_a, 0);
    CHECK_EQ(sos_task_get_stats(2u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_STOPPED);
}

/* max_job_steps: the longest AGAIN chain of one job, for checking the
 * max_steps assumption of the analysis on the target. */
void test_max_job_steps(void)
{
    static const sos_run_result_t script[] =
        { SOS_RUN_AGAIN, SOS_RUN_AGAIN, SOS_RUN_OK,
          SOS_RUN_AGAIN, SOS_RUN_AGAIN, SOS_RUN_AGAIN, SOS_RUN_AGAIN, SOS_RUN_OK,
          SOS_RUN_OK };
    sos_task_stats_t st;

    reset();
    g_d[0] = tdesc("j", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_t[0].script = script;
    g_t[0].script_len = 9u;
    CHECK_EQ(start(1u), SOS_OK);
    run_for(5000u);
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.max_job_steps, 5u);
}

/* F4: events posted before a task has started -- by its own init (e.g. an
 * interrupt it enables firing at once) or by an earlier task's init -- are
 * kept and delivered when the task starts, as on a restart. */
static sos_status_t post_early_init(void *ctx)
{
    tt_t *t = (tt_t *)ctx;
    t->inits++;
    if (sos_task_self() == 0u)
    {
        CHECK_EQ(sos_event_set_async(0u, 0x1u), SOS_OK);   /* to itself, mid-init */
        CHECK_EQ(sos_event_set(1u, 0x2u), SOS_OK);         /* to B, not started yet */
    }
    return SOS_OK;
}

void test_events_before_first_start(void)
{
    reset();
    g_d[0] = tdesc("A", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].init = post_early_init;
    g_d[1] = tdesc("B", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x2u, &g_t[1]);
    g_d[1].init = post_early_init;
    CHECK_EQ(start(2u), SOS_OK);
    run_for(100u);
    CHECK_EQ(g_t[0].runs, 1u);
    CHECK_EQ(g_t[0].last_ev, 0x1u);
    CHECK_EQ(g_t[1].runs, 1u);
    CHECK_EQ(g_t[1].last_ev, 0x2u);
    CHECK_EQ(sos_error_count(), 0u);
}

/* F1: the division-free fast path and the missed-period path give the same
 * release grid. */
void test_release_grid_fast_and_slow_path(void)
{
    static const uint64_t costs[] = { 100u, 2500u, 100u };
    unsigned i;
    sos_task_stats_t st;

    reset();
    g_d[0] = tdesc("p", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[0].on_error = SOS_ON_ERR_IGNORE;
    CHECK_EQ(start(1u), SOS_OK);
    for (i = 0u; i < 3u; i++)
    {
        g_t[0].cost = costs[i];
        run_for(1000u);
    }
    run_for(3000u);
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.miss_cnt, 2u);                          /* the 2.5 ms step skipped 2 */
    for (i = 0u; i < g_ntrace; i++)
    {
        CHECK_EQ(g_trace[i].at % 1000u, 0u);            /* always on the 1 ms grid */
    }
}
