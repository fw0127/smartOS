/* Scheduling scenario tests on virtual time (docs/03 sections 3-5). */
#include <string.h>
#include "test_util.h"

static tt_t g_t[6];
static sos_task_desc_t g_d[6];
static sos_cfg_root_t g_root;

static void setup(uint64_t start, uint8_t n)
{
    g_root = troot(g_d, n);
    CHECK_EQ(sos_init(&g_root), SOS_OK);
    trace_reset();
    (void)start;
}

static void reset_tasks(uint64_t start)
{
    fresh(start);
    memset(g_t, 0, sizeof g_t);
}

/* Two 10 ms tasks, the second offset by 5 ms: they never coincide, the
 * spacing is exact, and the result is the same across the tick wrap. */
static void periodic_offset(uint64_t start)
{
    unsigned i;
    uint64_t last0 = 0u, last1 = 0u;
    int n0 = 0, n1 = 0, bad = 0;

    reset_tasks(start);
    g_d[0] = tdesc("p0", 0u, SOS_TRIG_PERIODIC, 10000u, 0u, 0u, &g_t[0]);
    g_d[1] = tdesc("p1", 1u, SOS_TRIG_PERIODIC, 10000u, 5000u, 0u, &g_t[1]);
    setup(start, 2u);
    run_for(100000u);

    for (i = 0u; i < g_ntrace; i++)
    {
        uint64_t at = g_trace[i].at;
        if (g_trace[i].task == 0u) { if ((n0 > 0) && (at - last0 != 10000u)) bad++; last0 = at; n0++; }
        else                       { if ((n1 > 0) && (at - last1 != 10000u)) bad++; last1 = at; n1++; }
    }
    CHECK_EQ(n0, 10);
    CHECK_EQ(n1, 10);
    CHECK_EQ(g_trace[0].at, 0u);
    CHECK_EQ(g_trace[1].at, 5000u);
    CHECK_EQ(bad, 0);
    CHECK_EQ(host_critical_depth(), 0);
}

void test_periodic_offset(void)
{
    periodic_offset(0u);
}

void test_periodic_offset_across_wrap(void)
{
    periodic_offset(near_wrap(25000u));     /* wraps after 25 ms of a 100 ms run */
}

/* Released together: the higher priority runs first. */
void test_priority_order(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("hi", 0u, SOS_TRIG_PERIODIC, 10000u, 0u, 0u, &g_t[0]);
    g_d[1] = tdesc("lo", 1u, SOS_TRIG_PERIODIC, 10000u, 0u, 0u, &g_t[1]);
    g_t[0].cost = 100u;
    setup(0u, 2u);
    run_for(1000u);

    CHECK_EQ(g_ntrace, 2u);
    CHECK_EQ(g_trace[0].task, 0u);
    CHECK_EQ(g_trace[1].task, 1u);
    CHECK_EQ(g_trace[1].at, 100u);
}

/* A high-priority event arriving during a low-priority step is served as
 * soon as that one step ends, before the low task's next AGAIN step. */
static void raise_hi_on_first(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 1u)
    {
        CHECK_EQ(sos_event_set_async(0u, 0x1u), SOS_OK);    /* "interrupt" during the step */
    }
}

void test_event_preempts_between_steps(void)
{
    static const sos_run_result_t lo_script[] = { SOS_RUN_AGAIN, SOS_RUN_AGAIN, SOS_RUN_OK };

    reset_tasks(0u);
    g_d[0] = tdesc("hi", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[1] = tdesc("lo", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[1]);
    g_t[1].script = lo_script;
    g_t[1].script_len = 3u;
    g_t[1].cost = 300u;
    g_t[1].action = raise_hi_on_first;
    setup(0u, 2u);

    CHECK_EQ(sos_event_set(1u, 0x1u), SOS_OK);
    run_for(5000u);

    CHECK_EQ(g_ntrace, 4u);
    CHECK_EQ(g_trace[0].task, 1u);       /* lo step 1 (raises hi) */
    CHECK_EQ(g_trace[1].task, 0u);       /* hi right after that step */
    CHECK_EQ(g_trace[1].at, 300u);
    CHECK_EQ(g_trace[2].task, 1u);
    CHECK_EQ(g_trace[3].task, 1u);
    CHECK_EQ(g_t[0].last_ev, 0x1u);
}

/* Events set while a task runs are not lost: it runs again. */
static void post_self_once(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 1u)
    {
        CHECK_EQ(sos_event_set(sos_task_self(), 0x2u), SOS_OK);
    }
}

void test_event_during_own_run(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("e", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x3u, &g_t[0]);
    g_t[0].action = post_self_once;
    setup(0u, 1u);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(1000u);
    CHECK_EQ(g_t[0].runs, 2u);
    CHECK_EQ(g_t[0].last_ev, 0x2u);
}

/* Bits outside the mask are kept but do not wake the task. */
void test_event_mask(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("e", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    setup(0u, 1u);

    CHECK_EQ(sos_event_set(0u, 0x4u), SOS_OK);
    run_for(1000u);
    CHECK_EQ(g_t[0].runs, 0u);
    CHECK_EQ(sos_event_peek(0u), 0x4u);
    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(1000u);
    CHECK_EQ(g_t[0].runs, 1u);
    CHECK_EQ(g_t[0].last_ev, 0x5u);
    CHECK_EQ(sos_event_set(0u, SOS_EV_ALARM), SOS_E_PARAM);
}

/* An asynchronous event while the kernel is idle ends the wait at once. */
void test_async_event_wakes_idle(void)
{
    sos_tick_t dl = 0u;

    reset_tasks(0u);
    g_d[0] = tdesc("e", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    setup(0u, 1u);

    CHECK_EQ(sos_poll(&dl), SOS_POLL_IDLE);
    CHECK_EQ(sos_event_set_async(0u, 0x1u), SOS_OK);   /* lands before the wait */
    CHECK(host_notify_pending());
    sos_platform_wait_until(dl);
    CHECK_EQ(rel_now(), 0u);                            /* returned without sleeping */
    CHECK_EQ(sos_poll(&dl), SOS_POLL_RAN);
    CHECK_EQ(g_t[0].runs, 1u);
}

/* AGAIN yield: with again_limit = 3 the low task gets one step after every
 * three high steps; with no limit it starves. */
static void again_yield(uint16_t limit, unsigned *lo_runs, unsigned *hi_runs)
{
    reset_tasks(0u);
    g_d[0] = tdesc("busy", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].again_limit = limit;
    g_d[1] = tdesc("lo", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[1]);
    g_t[0].dflt = SOS_RUN_AGAIN;
    g_t[0].cost = 100u;
    g_t[1].dflt = SOS_RUN_AGAIN;
    g_t[1].cost = 100u;
    setup(0u, 2u);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    CHECK_EQ(sos_event_set(1u, 0x1u), SOS_OK);
    run_for(12000u);
    *hi_runs = g_t[0].runs;
    *lo_runs = g_t[1].runs;
}

void test_again_yield(void)
{
    unsigned lo, hi, i;
    int pattern_ok = 1;

    again_yield(3u, &lo, &hi);
    CHECK_EQ(hi + lo, 120u);
    CHECK_EQ(hi, 90u);
    CHECK_EQ(lo, 30u);
    for (i = 0u; (i + 3u) < g_ntrace; i += 4u)          /* busy busy busy lo ... */
    {
        if ((g_trace[i].task != 0u) || (g_trace[i + 1u].task != 0u)
            || (g_trace[i + 2u].task != 0u) || (g_trace[i + 3u].task != 1u))
        {
            pattern_ok = 0;
        }
    }
    CHECK(pattern_ok);

    again_yield(0u, &lo, &hi);
    CHECK_EQ(lo, 0u);                                    /* starved without a limit */
    CHECK_EQ(hi, 120u);
}

/* A yielding task that is alone must not make the kernel wait. */
void test_again_yield_alone_never_waits(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("busy", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].again_limit = 2u;
    g_t[0].dflt = SOS_RUN_AGAIN;
    g_t[0].cost = 10u;
    setup(0u, 1u);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(1000u);
    CHECK_EQ(g_t[0].runs, 100u);
    CHECK_EQ(host_wait_calls(), 0u);
}

/* The yielded opportunity goes to the background task when no lower
 * foreground task is ready; higher tasks are unaffected. */
void test_again_yield_to_background(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("busy", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[0].again_limit = 4u;
    g_d[1] = tdesc("bg", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_t[1]);
    g_t[0].dflt = SOS_RUN_AGAIN;
    g_t[0].cost = 100u;
    g_t[1].cost = 100u;
    setup(0u, 2u);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(5000u);
    CHECK_EQ(g_t[0].runs, 40u);
    CHECK_EQ(g_t[1].runs, 10u);
}

/* Background tasks only fill idle time; a foreground release waits at most
 * one background step. */
void test_background_fills_idle(void)
{
    unsigned i;
    int late = 0;

    reset_tasks(0u);
    g_d[0] = tdesc("p", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[1] = tdesc("bg1", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_t[1]);
    g_d[2] = tdesc("bg2", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_t[2]);
    g_t[0].cost = 200u;
    g_t[1].cost = 70u;
    g_t[2].cost = 70u;
    setup(0u, 3u);
    run_for(10000u);

    CHECK_EQ(g_t[0].runs, 10u);
    for (i = 0u; i < g_ntrace; i++)
    {
        if ((g_trace[i].task == 0u) && ((g_trace[i].at % 1000u) > 70u))
        {
            late++;
        }
    }
    CHECK_EQ(late, 0);
    CHECK(g_t[1].runs > 50u);
    CHECK(((int)g_t[1].runs - (int)g_t[2].runs) <= 1);   /* round robin */
    CHECK(((int)g_t[2].runs - (int)g_t[1].runs) <= 1);
    CHECK_EQ(host_wait_calls(), 0u);
}

/* Alarm: delivered as SOS_EV_ALARM after the delay; cancel suppresses it. */
static void set_alarm_first(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 1u)
    {
        CHECK_EQ(sos_task_set_alarm(3000u), SOS_OK);
        if (self->user != 0)
        {
            CHECK_EQ(sos_task_cancel_alarm(), SOS_OK);
        }
    }
}

void test_alarm(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("a", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_t[0].action = set_alarm_first;
    setup(0u, 1u);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(10000u);
    CHECK_EQ(g_t[0].runs, 2u);
    CHECK_EQ(g_t[0].last_ev, SOS_EV_ALARM);
    CHECK_EQ(g_t[0].last_run_at, 3000u);

    reset_tasks(0u);
    g_t[0].action = set_alarm_first;
    g_t[0].user = 1;
    setup(0u, 1u);
    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(10000u);
    CHECK_EQ(g_t[0].runs, 1u);
    CHECK_EQ(sos_task_set_alarm(10u), SOS_E_STATE);      /* not in task context */
}

/* A step longer than the period: misses are counted, nothing is replayed. */
void test_missed_periods(void)
{
    sos_task_stats_t st;

    reset_tasks(0u);
    g_d[0] = tdesc("slow", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_t[0].cost = 3500u;
    setup(0u, 1u);
    run_for(20000u);

    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(g_t[0].runs, 5u);                          /* at 0, 4, 8, 12, 16 ms */
    CHECK_EQ(g_trace[1].at, 4000u);
    CHECK_EQ(st.miss_cnt, 15u);
    CHECK_EQ(st.max_step, 3500u);
}

/* Periodic + event: an event-triggered step does not shift the period grid. */
void test_periodic_and_event(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("pe", 0u, SOS_TRIG_PERIODIC | SOS_TRIG_EVENT, 1000u, 0u, 0x1u, &g_t[0]);
    setup(0u, 1u);

    run_for(300u);
    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(2800u);
    CHECK_EQ(g_ntrace, 5u);
    CHECK_EQ(g_trace[0].at, 0u);
    CHECK_EQ(g_trace[1].at, 300u);
    CHECK_EQ(g_trace[2].at, 1000u);
    CHECK_EQ(g_trace[3].at, 2000u);
    CHECK_EQ(g_trace[4].at, 3000u);
}

/* Stop: by return value, by request on self, by request on another task. */
static void stop_self_second(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 2u)
    {
        CHECK_EQ(sos_task_stop(sos_task_self()), SOS_OK);
    }
}

void test_stop(void)
{
    sos_task_stats_t st;
    static const sos_run_result_t stop_script[] = { SOS_RUN_OK, SOS_RUN_STOP };

    reset_tasks(0u);
    g_d[0] = tdesc("ret", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[0]);
    g_d[1] = tdesc("req", 1u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[1]);
    g_d[2] = tdesc("ext", 2u, SOS_TRIG_PERIODIC, 1000u, 0u, 0u, &g_t[2]);
    g_t[0].script = stop_script;
    g_t[0].script_len = 2u;
    g_t[1].action = stop_self_second;
    setup(0u, 3u);

    run_for(500u);
    CHECK_EQ(sos_task_stop(2u), SOS_OK);
    run_for(9500u);

    CHECK_EQ(g_t[0].runs, 2u);
    CHECK_EQ(g_t[0].dones, 1u);
    CHECK_EQ(g_t[1].runs, 2u);
    CHECK_EQ(g_t[1].dones, 1u);
    CHECK_EQ(g_t[2].runs, 1u);
    CHECK_EQ(g_t[2].dones, 1u);
    CHECK_EQ(sos_task_get_stats(2u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_STOPPED);
    CHECK_EQ(sos_event_set(2u, 0x1u), SOS_E_STATE);
    CHECK_EQ(sos_task_stop(2u), SOS_E_STATE);

    CHECK_EQ(sos_task_restart(2u), SOS_OK);              /* restart a stopped task */
    run_for(1000u);
    CHECK_EQ(g_t[2].inits, 2u);
    CHECK_EQ(g_t[2].runs, 2u);
}

/* Stats report READY / WAITING from the bitmap. */
void test_stats_state(void)
{
    sos_task_stats_t st;

    reset_tasks(0u);
    g_d[0] = tdesc("e", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    setup(0u, 1u);
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_WAITING);
    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    CHECK_EQ(sos_task_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.state, SOS_STATE_READY);
    CHECK_EQ(sos_task_get_stats(7u, &st), SOS_E_PARAM);
}

/* A periodic-only task is not woken by events; the bits arrive with its
 * next periodic step. */
void test_event_to_periodic_only(void)
{
    reset_tasks(0u);
    g_d[0] = tdesc("p", 0u, SOS_TRIG_PERIODIC, 1000u, 0u, 0x1u, &g_t[0]);
    setup(0u, 1u);

    run_for(300u);                                      /* first step at 0 */
    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(500u);
    CHECK_EQ(g_t[0].runs, 1u);                          /* not woken */
    run_for(1000u);
    CHECK_EQ(g_t[0].runs, 2u);
    CHECK_EQ(g_t[0].last_ev, 0x1u);
    CHECK_EQ(g_trace[1].at, 1000u);
}

/* D13: A yields while a higher-priority H is ready. H runs first (strict
 * priority), but that does not consume A's yield: the opportunity still goes
 * to the lower-priority B. */
static void a_wakes_h_on_yield(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 2u)
    {
        CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);    /* H ready exactly at A's yield */
    }
}

void test_yield_survives_higher_priority(void)
{
    unsigned i;
    int b_after_h = -1;

    reset_tasks(0u);
    g_d[0] = tdesc("H", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_d[1] = tdesc("A", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[1]);
    g_d[1].again_limit = 2u;
    g_d[2] = tdesc("B", 2u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[2]);
    g_t[1].dflt = SOS_RUN_AGAIN;
    g_t[1].cost = 100u;
    g_t[1].action = a_wakes_h_on_yield;
    g_t[2].dflt = SOS_RUN_AGAIN;
    g_t[2].cost = 100u;
    setup(0u, 3u);

    CHECK_EQ(sos_event_set(1u, 0x1u), SOS_OK);
    CHECK_EQ(sos_event_set(2u, 0x1u), SOS_OK);
    run_for(450u);                                      /* A A H B ... */
    for (i = 0u; (i + 1u) < g_ntrace; i++)
    {
        if (g_trace[i].task == 0u)
        {
            b_after_h = g_trace[i + 1u].task;
        }
    }
    CHECK_EQ(g_trace[0].task, 1u);
    CHECK_EQ(g_trace[1].task, 1u);
    CHECK_EQ(g_trace[2].task, 0u);                      /* H first: strict priority */
    CHECK_EQ(b_after_h, 2);                             /* then B, not A again */
}

