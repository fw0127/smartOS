/* Ring buffer tests on the virtual platform (docs/03 section 6.2). */
#include <string.h>
#include "test_util.h"

#if SOS_CFG_RING_ENABLE

static tt_t g_t[2];
static sos_task_desc_t g_d[2];
static sos_ring_desc_t g_rd[10];
static sos_cfg_root_t g_root;
static uint32_t g_store[8];
#if SOS_CFG_RING_ZERO_COPY
static uint8_t g_big[4][64];
#endif
static int g_overflow_hook;

static void on_ring_overflow(uint8_t ring)
{
    (void)ring;
    g_overflow_hook++;
}

static const sos_hooks_t g_hooks = { NULL, NULL, NULL, NULL, on_ring_overflow };

static sos_ring_desc_t rdesc(void *storage, uint16_t elem, uint16_t cap,
                             sos_task_id_t consumer, sos_events_t notify)
{
    sos_ring_desc_t d;
    memset(&d, 0, sizeof d);
    d.name = "r";
    d.storage = storage;
    d.elem_size = elem;
    d.capacity = cap;
    d.producer = SOS_TASK_NONE;               /* asynchronous context */
    d.consumer = consumer;
    d.notify = notify;
    return d;
}

/* one idle event task + n rings */
static sos_status_t start(uint8_t nrings)
{
    fresh(0u);
    memset(g_t, 0, sizeof g_t);
    g_overflow_hook = 0;
    g_d[0] = tdesc("c", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_t[0]);
    g_root = troot(g_d, 1u);
    g_root.rings = g_rd;
    g_root.ring_count = nrings;
    g_root.hooks = &g_hooks;
    return sos_init(&g_root);
}

void test_ring_fifo_full_empty(void)
{
    uint32_t v, i;
    sos_ring_stats_t st;
    int order_ok = 1;

    g_rd[0] = rdesc(g_store, sizeof(uint32_t), 8u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_OK);

    CHECK_EQ(sos_ring_get(0u, &v), SOS_E_EMPTY);
    for (i = 0u; i < 8u; i++)
    {
        CHECK_EQ(sos_ring_put(0u, &i), SOS_OK);
    }
    CHECK_EQ(sos_ring_put(0u, &i), SOS_E_FULL);
    CHECK_EQ(sos_ring_count(0u), 8u);
    CHECK_EQ(sos_ring_peek(0u, &v), SOS_OK);
    CHECK_EQ(v, 0u);
    CHECK_EQ(sos_ring_count(0u), 8u);                 /* peek does not remove */
    for (i = 0u; i < 8u; i++)
    {
        CHECK_EQ(sos_ring_get(0u, &v), SOS_OK);
        if (v != i) { order_ok = 0; }
    }
    CHECK(order_ok);
    CHECK_EQ(sos_ring_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.capacity, 8u);
    CHECK_EQ(st.count, 0u);
    CHECK_EQ(st.high_water, 8u);
    CHECK_EQ(st.overflow_cnt, 1u);
    CHECK_EQ(g_overflow_hook, 1);
    CHECK_EQ(sos_ring_put(1u, &i), SOS_E_PARAM);       /* no such ring */
}

/* The 16-bit free-running indices wrap many times without losing order. */
void test_ring_index_wrap(void)
{
    uint32_t i, v;
    int bad = 0;

    g_rd[0] = rdesc(g_store, sizeof(uint32_t), 8u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_OK);
    for (i = 0u; i < 200000u; i++)
    {
        if (sos_ring_put(0u, &i) != SOS_OK) { bad++; }
        if ((i % 3u) == 2u)                         /* keep 0..3 elements queued */
        {
            while (sos_ring_count(0u) > 1u)
            {
                (void)sos_ring_get(0u, &v);
            }
        }
    }
    while (sos_ring_get(0u, &v) == SOS_OK) { }
    CHECK_EQ(v, 199999u);
    CHECK_EQ(bad, 0);
}

#if SOS_CFG_RING_ZERO_COPY
void test_ring_zero_copy(void)
{
    uint8_t *w, *w2;
    const uint8_t *r, *r2;
    uint8_t tmp[64];
    sos_ring_stats_t st;

    g_rd[0] = rdesc(g_big, 64u, 4u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_OK);

    CHECK(sos_ring_acquire(0u) == NULL);              /* empty */
    CHECK_EQ(sos_ring_commit(0u), SOS_E_STATE);        /* nothing reserved */
    CHECK_EQ(sos_ring_release(0u), SOS_E_STATE);       /* nothing acquired */

    w = (uint8_t *)sos_ring_reserve(0u);
    CHECK(w != NULL);
    w2 = (uint8_t *)sos_ring_reserve(0u);
    CHECK(w == w2);                                   /* idempotent */
    CHECK_EQ(sos_ring_put(0u, tmp), SOS_E_STATE);      /* no mixing on this side */
    memset(w, 0xA5, 64u);
    CHECK_EQ(sos_ring_count(0u), 0u);                 /* not visible before commit */
    CHECK_EQ(sos_ring_commit(0u), SOS_OK);
    CHECK_EQ(sos_ring_count(0u), 1u);

    r = (const uint8_t *)sos_ring_acquire(0u);
    CHECK(r == w);                                    /* in place, no copy */
    r2 = (const uint8_t *)sos_ring_acquire(0u);
    CHECK(r == r2);
    CHECK_EQ(r[63], 0xA5u);
    CHECK_EQ(sos_ring_get(0u, tmp), SOS_E_STATE);
    CHECK_EQ(sos_ring_release(0u), SOS_OK);
    CHECK_EQ(sos_ring_count(0u), 0u);

    /* fill with reserve/commit, then reserve fails and counts an overflow */
    while ((w = (uint8_t *)sos_ring_reserve(0u)) != NULL)
    {
        (void)sos_ring_commit(0u);
    }
    CHECK_EQ(sos_ring_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.count, 4u);
    CHECK_EQ(st.overflow_cnt, 1u);
}
#endif

/* A consumer bound to the ring is woken by each publish; it drains at most
 * K = 4 elements per step and returns AGAIN while data remains. */
static uint32_t g_seen[32];
static uint32_t g_nseen;

static void drain_k(tt_t *self, sos_events_t ev)
{
    uint32_t v;
    unsigned k = 0u;
    (void)ev;
    while ((k < 4u) && (sos_ring_get(0u, &v) == SOS_OK))
    {
        g_seen[g_nseen++] = v;
        k++;
    }
    self->user = (sos_ring_count(0u) != 0u) ? 1 : 0;
}

static sos_run_result_t drain_run(void *ctx, sos_events_t ev)
{
    tt_t *t = (tt_t *)ctx;
    (void)tt_run(ctx, ev);
    return (t->user != 0) ? SOS_RUN_AGAIN : SOS_RUN_OK;
}

void test_ring_notifies_consumer(void)
{
    uint32_t i;
    int order_ok = 1;

    g_rd[0] = rdesc(g_store, sizeof(uint32_t), 8u, 0u, 0x1u);
    CHECK_EQ(start(1u), SOS_OK);                       /* re-init below with drain_run */
    g_d[0].run = drain_run;
    g_t[0].action = drain_k;
    CHECK_EQ(sos_init(&g_root), SOS_OK);
    g_nseen = 0u;
    trace_reset();

    for (i = 0u; i < 8u; i++)
    {
        CHECK_EQ(sos_ring_put(0u, &i), SOS_OK);        /* e.g. from an interrupt */
    }
    CHECK(host_notify_pending());
    run_for(1000u);
    CHECK_EQ(g_nseen, 8u);
    CHECK_EQ(g_t[0].runs, 2u);                        /* 4 + 4 */
    for (i = 0u; i < g_nseen; i++)
    {
        if (g_seen[i] != i) { order_ok = 0; }
    }
    CHECK(order_ok);
}

void test_ring_cfg_rejects(void)
{
    uint8_t i;

    g_rd[0] = rdesc(g_store, 4u, 6u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* not a power of two */
    g_rd[0] = rdesc(NULL, 4u, 8u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* no storage */
    g_rd[0] = rdesc(g_store, 0u, 8u, SOS_TASK_NONE, 0u);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* zero element size */
    g_rd[0] = rdesc(g_store, 4u, 8u, 5u, 0x1u);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* unknown consumer */
    g_rd[0] = rdesc(g_store, 4u, 8u, 0u, SOS_EV_ALARM);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* reserved event bit */
    g_rd[0] = rdesc(g_store, 4u, 8u, SOS_TASK_NONE, 0x1u);
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* notify without a consumer */
    g_rd[0] = rdesc(g_store, 4u, 8u, 0u, 0u);
    g_rd[0].producer = 0u;
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* producer == consumer */
    g_rd[0].producer = 7u;
    CHECK_EQ(start(1u), SOS_E_CFG);                    /* unknown producer */
    for (i = 0u; i < 10u; i++)
    {
        g_rd[i] = rdesc(g_store, 4u, 8u, SOS_TASK_NONE, 0u);
    }
    CHECK_EQ(start(9u), SOS_E_CFG);                    /* more than SOS_CFG_MAX_RINGS */
    CHECK_EQ(start(8u), SOS_OK);
    g_root.rings = NULL;
    CHECK_EQ(sos_init(&g_root), SOS_E_CFG);            /* count without table */
}

#if SOS_CFG_RING_ZERO_COPY
/* D11: a producer task that fails between reserve and commit, or a consumer
 * task that fails between acquire and release, is restarted with a clean
 * ring state: the reserved slot is discarded, the acquired element stays
 * queued and is delivered again. */
static tt_t g_own[2];
static sos_task_desc_t g_od[2];

static void prod_reserve_then_fail(tt_t *self, sos_events_t ev)
{
    (void)ev;
    if (self->runs == 1u)
    {
        CHECK(sos_ring_reserve(0u) != NULL);        /* never committed */
    }
    else
    {
        uint32_t v = 7u;
        CHECK_EQ(sos_ring_put(0u, &v), SOS_OK);     /* would be E_STATE without D11 */
    }
}

static void cons_acquire_then_fail(tt_t *self, sos_events_t ev)
{
    const uint32_t *p = (const uint32_t *)sos_ring_acquire(0u);
    (void)ev;
    CHECK(p != NULL);
    if ((p != NULL) && (self->runs >= 2u))
    {
        self->user = (int)*p;
        CHECK_EQ(sos_ring_release(0u), SOS_OK);
    }
}

void test_ring_owner_restart(void)
{
    static const sos_run_result_t fail_first[] = { SOS_RUN_ERR, SOS_RUN_OK };
    sos_ring_stats_t st;

    fresh(0u);
    memset(g_own, 0, sizeof g_own);
    g_od[0] = tdesc("prod", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_own[0]);
    g_od[0].on_error = SOS_ON_ERR_RESTART;
    g_od[0].max_restart = 2u;
    g_own[0].script = fail_first;
    g_own[0].script_len = 2u;
    g_own[0].action = prod_reserve_then_fail;
    g_od[1] = tdesc("cons", 1u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_own[1]);
    g_od[1].on_error = SOS_ON_ERR_RESTART;
    g_od[1].max_restart = 2u;
    g_own[1].script = fail_first;
    g_own[1].script_len = 2u;
    g_own[1].action = cons_acquire_then_fail;
    g_rd[0] = rdesc(g_store, sizeof(uint32_t), 8u, 1u, 0x1u);
    g_rd[0].producer = 0u;
    g_root = troot(g_od, 2u);
    g_root.rings = g_rd;
    g_root.ring_count = 1u;
    CHECK_EQ(sos_init(&g_root), SOS_OK);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);       /* producer: reserve, ERR, restart */
    run_for(100u);
    CHECK_EQ(g_own[0].inits, 2u);
    CHECK_EQ(sos_ring_count(0u), 0u);                 /* the reserved slot was discarded */
    CHECK_EQ(g_own[1].runs, 0u);                      /* nothing was published */

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);       /* producer: put 7 -> wakes consumer */
    run_for(100u);                                    /* consumer: acquire, ERR, restart, */
    CHECK_EQ(g_own[1].inits, 2u);                     /* then acquires the same element */
    CHECK_EQ(g_own[1].runs, 1u);
    CHECK_EQ(sos_ring_count(0u), 1u);                 /* element kept */
    CHECK_EQ(sos_event_set(1u, 0x1u), SOS_OK);
    run_for(100u);
    CHECK_EQ(g_own[1].user, 7);
    CHECK_EQ(sos_ring_get_stats(0u, &st), SOS_OK);
    CHECK_EQ(st.count, 0u);
}

void test_ring_owner_stop(void)
{
    fresh(0u);
    memset(g_own, 0, sizeof g_own);
    g_od[0] = tdesc("prod", 0u, SOS_TRIG_EVENT, 0u, 0u, 0x1u, &g_own[0]);
    g_od[0].on_error = SOS_ON_ERR_STOP;
    g_own[0].dflt = SOS_RUN_ERR;
    g_own[0].action = prod_reserve_then_fail;
    g_rd[0] = rdesc(g_store, sizeof(uint32_t), 8u, SOS_TASK_NONE, 0u);
    g_rd[0].producer = 0u;
    g_root = troot(g_od, 1u);
    g_root.rings = g_rd;
    g_root.ring_count = 1u;
    CHECK_EQ(sos_init(&g_root), SOS_OK);

    CHECK_EQ(sos_event_set(0u, 0x1u), SOS_OK);
    run_for(100u);
    CHECK_EQ(g_own[0].dones, 1u);                     /* stopped */
    {
        uint32_t v = 9u;                              /* e.g. a replacement producer */
        CHECK_EQ(sos_ring_put(0u, &v), SOS_OK);       /* E_STATE if the reservation leaked */
    }
    CHECK_EQ(sos_ring_count(0u), 1u);
}
#endif

#endif /* SOS_CFG_RING_ENABLE */

