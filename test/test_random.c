/*
 * Randomised state-machine tests with invariant checks (virtual time).
 *
 *  test_random_ring       random put/get/peek/reserve/commit/acquire/release
 *                         against a reference queue model
 *  test_random_scheduler  random events, stops, restarts, alarms, results and
 *                         time; after every kernel poll the scheduler's
 *                         invariants are checked (incl. the next_due cache)
 *
 * Fixed seeds: every run is reproducible; a failure prints its seed.
 */
#include <string.h>
#include "test_util.h"
#include "../kernel/src/sos_kernel_pri.h"

static uint32_t l_rng;
static uint32_t rnd(uint32_t n)                      /* 0 .. n-1 */
{
    l_rng = l_rng * 1664525u + 1013904223u;
    return (l_rng >> 8) % n;
}

/* ---- ring ------------------------------------------------------------------ */
#if SOS_CFG_RING_ENABLE

#define RCAP 8u
static uint32_t        g_rstore[RCAP];
static sos_ring_desc_t g_rdesc;
static tt_t            g_rt;
static sos_task_desc_t g_rtd[1];
static sos_cfg_root_t  g_rroot;

void test_random_ring(void)
{
    uint32_t seed;
    int bad = 0;

    for (seed = 1u; (seed <= 60u) && (bad == 0); seed++)
    {
        uint32_t model[RCAP];
        uint32_t head = 0u, len = 0u, next = 1u, op;
        bool reserved = false, acquired = false;
        const uint32_t *aslot = NULL;
#if SOS_CFG_RING_ZERO_COPY
        uint32_t *rslot = NULL;
#endif

        l_rng = seed;
        fresh(0u);
        memset(&g_rt, 0, sizeof g_rt);
        memset(&g_rdesc, 0, sizeof g_rdesc);
        g_rdesc.storage = g_rstore;
        g_rdesc.elem_size = sizeof(uint32_t);
        g_rdesc.capacity = RCAP;
        g_rdesc.producer = SOS_TASK_NONE;
        g_rdesc.consumer = SOS_TASK_NONE;
        g_rtd[0] = tdesc("t", 0u, SOS_TRIG_EVENT, 0u, 0u, 1u, &g_rt);
        g_rroot = troot(g_rtd, 1u);
        g_rroot.rings = &g_rdesc;
        g_rroot.ring_count = 1u;
        if (sos_init(&g_rroot) != SOS_OK) { bad++; break; }

        for (op = 0u; (op < 5000u) && (bad == 0); op++)
        {
            uint32_t v = 0u;
            sos_status_t st;
            switch (rnd(SOS_CFG_RING_ZERO_COPY ? 7u : 3u))
            {
                case 0u:                                         /* put */
                    st = sos_ring_put(0u, &next);
                    if (reserved)                 { bad += (st != SOS_E_STATE); }
                    else if (len == RCAP)         { bad += (st != SOS_E_FULL); }
                    else { bad += (st != SOS_OK); model[(head + len) % RCAP] = next; len++; next++; }
                    break;
                case 1u:                                         /* get */
                    st = sos_ring_get(0u, &v);
                    if (acquired)                 { bad += (st != SOS_E_STATE); }
                    else if (len == 0u)           { bad += (st != SOS_E_EMPTY); }
                    else { bad += (st != SOS_OK) || (v != model[head]); head = (head + 1u) % RCAP; len--; }
                    break;
                case 2u:                                         /* peek */
                    st = sos_ring_peek(0u, &v);
                    if (len == 0u)                { bad += (st != SOS_E_EMPTY); }
                    else                          { bad += (st != SOS_OK) || (v != model[head]); }
                    break;
#if SOS_CFG_RING_ZERO_COPY
                case 3u:                                         /* reserve (+ write) */
                {
                    uint32_t *p = (uint32_t *)sos_ring_reserve(0u);
                    if (reserved)                 { bad += (p != rslot); }
                    else if (len == RCAP)         { bad += (p != NULL); }
                    else { bad += (p == NULL); rslot = p; reserved = (p != NULL); }
                    if (p != NULL) { *p = next; }
                    break;
                }
                case 4u:                                         /* commit */
                    st = sos_ring_commit(0u);
                    if (!reserved)                { bad += (st != SOS_E_STATE); }
                    else { bad += (st != SOS_OK); model[(head + len) % RCAP] = next; len++; next++; reserved = false; }
                    break;
                case 5u:                                         /* acquire */
                {
                    const uint32_t *p = (const uint32_t *)sos_ring_acquire(0u);
                    if (len == 0u)                { bad += (p != NULL); }
                    else { bad += (p == NULL) || (*p != model[head]) || (acquired && (p != aslot));
                           aslot = p; acquired = (p != NULL); }
                    break;
                }
                case 6u:                                         /* release */
                    st = sos_ring_release(0u);
                    if (!acquired)                { bad += (st != SOS_E_STATE); }
                    else { bad += (st != SOS_OK); head = (head + 1u) % RCAP; len--; acquired = false; }
                    break;
#endif
                default:
                    break;
            }
            /* invariants */
            bad += (sos_ring_count(0u) != len);                 /* uncommitted data invisible */
            bad += (len > RCAP);
            if (acquired) { bad += (*aslot != model[head]); }   /* held element never overwritten */
        }
        if (bad != 0) { printf("    seed %u op %u\n", (unsigned)seed, (unsigned)op); }
    }
    CHECK_EQ(bad, 0);
}

#endif /* SOS_CFG_RING_ENABLE */

/* ---- scheduler ------------------------------------------------------------- */

#define NFG 6u
#define NT  (NFG + 1u)
static tt_t            g_st[NT];
static sos_task_desc_t g_sd[NT];
static sos_cfg_root_t  g_sroot;
static int             g_ran_inactive;

static sos_run_result_t rnd_run(void *ctx, sos_events_t ev)
{
    tt_t *t = (tt_t *)ctx;
    sos_task_id_t self = sos_task_self();
    uint32_t r;
    (void)ev;

    if (sos_k.tcb[self].state != (uint8_t)SOS_STATE_WAITING)
    {
        g_ran_inactive++;                                   /* dispatched while stopped */
    }
    t->runs++;
    host_advance(rnd(150u));
    switch (rnd(8u))                                        /* side effects from task context */
    {
        case 0u: (void)sos_event_set((sos_task_id_t)rnd(NT), 1u << rnd(3u)); break;
        case 1u: if (self < NFG) { (void)sos_task_set_alarm(rnd(2000u) + 1u); } break;
        case 2u: if (self < NFG) { (void)sos_task_cancel_alarm(); } break;
        case 3u: if (rnd(10u) == 0u) { (void)sos_task_stop((sos_task_id_t)rnd(NT)); } break;
        case 4u: if (rnd(10u) == 0u) { (void)sos_task_restart((sos_task_id_t)rnd(NT)); } break;
        default: break;
    }
    r = rnd(20u);
    return (r < 12u) ? SOS_RUN_OK : (r < 19u) ? SOS_RUN_AGAIN : SOS_RUN_ERR;
}

static int l_check_invariants(void)
{
    sos_tick_t now = sos_platform_now();
    uint32_t fg_active = 0u;
    uint8_t i;
    int bad = 0;

    bad += (host_critical_depth() != 0);
    for (i = 0u; i < NFG; i++)
    {
        const sos_tcb_t *t = &sos_k.tcb[i];
        const sos_task_desc_t *d = &g_sd[i];
        bool ready = (sos_k.ready_bits & (1u << i)) != 0u;
        bool scan_pending = sos_k.next_due_valid && sos_i_reached(now, sos_k.next_due);

        if (t->state != (uint8_t)SOS_STATE_WAITING)
        {
            continue;
        }
        fg_active |= 1u << i;
        /* an event that matches the mask has made the task ready */
        if (((d->trigger & SOS_TRIG_EVENT) != 0u) && ((t->events & d->event_mask) != 0u))
        {
            bad += !ready;
        }
        /* a reached release is either ready or about to be found by the scan */
        if (((d->trigger & SOS_TRIG_PERIODIC) != 0u) && sos_i_reached(now, t->next_release))
        {
            bad += !(ready || scan_pending);
        }
        /* an expired alarm is about to be found by the scan */
        if (((t->flags & SOS_F_ALARM) != 0u) && sos_i_reached(now, t->alarm_at))
        {
            bad += !scan_pending;
        }
    }
    bad += ((sos_k.ready_bits & ~fg_active) != 0u);         /* only active tasks are ready */
    bad += ((sos_k.deferred_bits & ~((1u << NFG) - 1u)) != 0u);
    return bad;
}

static void l_random_cfg(void)
{
    uint8_t i;
    static const uint8_t policies[] = { SOS_ON_ERR_RESTART, SOS_ON_ERR_IGNORE, SOS_ON_ERR_STOP };

    memset(g_st, 0, sizeof g_st);
    for (i = 0u; i < NFG; i++)
    {
        uint8_t trig = (uint8_t)(rnd(3u) + 1u);              /* periodic, event or both */
        g_sd[i] = tdesc("r", i, trig, 0u, 0u, 0u, &g_st[i]);
        if ((trig & SOS_TRIG_PERIODIC) != 0u) { g_sd[i].period = 200u + rnd(2800u); }
        if ((trig & SOS_TRIG_EVENT) != 0u)    { g_sd[i].event_mask = 1u + rnd(7u); }
        g_sd[i].offset = rnd(1000u);
        g_sd[i].again_limit = (uint16_t)rnd(4u);
        g_sd[i].on_error = policies[rnd(3u)];
        g_sd[i].max_restart = 255u;
        g_sd[i].run = rnd_run;
    }
    g_sd[NFG] = tdesc("bg", 0u, SOS_TRIG_BACKGROUND, 0u, 0u, 0u, &g_st[NFG]);
    g_sd[NFG].on_error = SOS_ON_ERR_IGNORE;
    g_sd[NFG].run = rnd_run;
    g_sroot = troot(g_sd, NT);
}

void test_random_scheduler(void)
{
    uint32_t seed;
    int bad = 0;
    uint32_t halted = 0u;

    for (seed = 1u; (seed <= 120u) && (bad == 0) && (halted == 0u); seed++)
    {
        uint32_t it;
        l_rng = seed * 7919u;
        fresh(near_wrap(500000u + rnd(1000000u)));          /* crosses the wrap mid-run */
        l_random_cfg();
        g_ran_inactive = 0;
        if (sos_init(&g_sroot) != SOS_OK) { bad++; break; }

        host_halted = false;
        if (setjmp(host_halt_jb) == 0)
        {
            host_halt_armed = true;
            for (it = 0u; (it < 3000u) && (bad == 0); it++)
            {
                sos_tick_t dl;
                switch (rnd(10u))                           /* asynchronous / external stimuli */
                {
                    case 0u: (void)sos_event_set_async((sos_task_id_t)rnd(NT), 1u << rnd(3u)); break;
                    case 1u: if (rnd(8u) == 0u) { (void)sos_task_stop((sos_task_id_t)rnd(NT)); } break;
                    case 2u: if (rnd(8u) == 0u) { (void)sos_task_restart((sos_task_id_t)rnd(NT)); } break;
                    case 3u: host_advance(rnd(500u)); break;
                    default: break;
                }
                if (sos_poll(&dl) == SOS_POLL_IDLE)
                {
                    sos_platform_wait_until(dl);
                }
                bad += l_check_invariants();
                if (bad != 0) { printf("    seed %u iteration %u\n", (unsigned)seed, (unsigned)it); }
            }
            host_halt_armed = false;
        }
        if (host_halted) { halted = seed; }
        bad += g_ran_inactive;
    }
    CHECK_EQ(halted, 0u);                                   /* no unexpected fatal */
    CHECK_EQ(bad, 0);
}
