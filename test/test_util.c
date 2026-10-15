#include <string.h>
#include <stdlib.h>
#include "test_util.h"

int t_fail;
int t_checks;

trace_t  g_trace[TRACE_MAX];
unsigned g_ntrace;
static uint64_t l_t0;

void trace_reset(void)
{
    g_ntrace = 0u;
    l_t0 = host_abs_now();
}

uint64_t rel_now(void)
{
    return host_abs_now() - l_t0;
}

void fresh(uint64_t start_abs)
{
    host_reset(start_abs);
    trace_reset();
}

uint64_t near_wrap(uint64_t below)
{
#if SOS_PLATFORM_TICK_BITS == 64
    return (uint64_t)0 - below;
#else
    return ((uint64_t)1 << SOS_PLATFORM_TICK_BITS) - below;
#endif
}

sos_status_t tt_init(void *ctx)
{
    tt_t *t = (tt_t *)ctx;
    t->inits++;
    if (t->init_failures != 0u)
    {
        t->init_failures--;
        return SOS_E_INIT;
    }
    return SOS_OK;
}

sos_run_result_t tt_run(void *ctx, sos_events_t ev)
{
    tt_t *t = (tt_t *)ctx;
    sos_run_result_t r;

    if (g_ntrace < TRACE_MAX)
    {
        g_trace[g_ntrace].task = sos_task_self();
        g_trace[g_ntrace].at = rel_now();
        g_ntrace++;
    }
    t->last_ev = ev;
    t->last_run_at = rel_now();
    host_advance(t->cost);
    r = (t->runs < t->script_len) ? t->script[t->runs] : t->dflt;
    t->runs++;
    if (t->action != NULL)
    {
        t->action(t, ev);
    }
    return r;
}

void tt_done(void *ctx)
{
    ((tt_t *)ctx)->dones++;
}

sos_task_desc_t tdesc(const char *name, uint8_t prio, uint8_t trigger,
                      sos_time_t period, sos_time_t offset, sos_events_t mask,
                      tt_t *ctx)
{
    sos_task_desc_t d;
    memset(&d, 0, sizeof d);
    d.name = name;
    d.prio = prio;
    d.trigger = trigger;
    d.period = period;
    d.offset = offset;
    d.event_mask = mask;
    d.on_error = SOS_ON_ERR_FATAL;
    d.init = tt_init;
    d.run = tt_run;
    d.done = tt_done;
    d.ctx = ctx;
    return d;
}

sos_cfg_root_t troot(const sos_task_desc_t *tasks, uint8_t n)
{
    sos_cfg_root_t r;
    memset(&r, 0, sizeof r);
    r.abi_version = SOS_ABI_VERSION;
    r.tasks = tasks;
    r.task_count = n;
    return r;
}

void run_for(uint64_t duration)
{
    uint64_t start = host_abs_now();
    unsigned long guard = 0ul;

    while ((host_abs_now() - start) < duration)
    {
        sos_tick_t dl = 0u;
        sos_poll_result_t r = sos_poll(&dl);

        if (++guard > 2000000ul)
        {
            printf("    FAIL run_for: no progress\n");
            t_fail++;
            return;
        }
        if (r == SOS_POLL_IDLE)
        {
            uint64_t left = duration - (host_abs_now() - start);
            uint64_t dist = (uint64_t)(sos_tick_t)(dl - sos_platform_now());
            if (dist >= left)
            {
                host_advance(left);          /* stop at the end of the window */
            }
            else
            {
                sos_platform_wait_until(dl);
            }
        }
    }
}
