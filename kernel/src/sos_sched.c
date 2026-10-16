/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Cooperative run-to-completion scheduler (docs/03 sections 3-5, 9, 10).
 *
 * Foreground tasks occupy table slots 0..fg_count-1 in ascending priority;
 * slot i is bit i of the ready bitmap. Background tasks follow and run only
 * in the idle slot, round robin.
 */
#include "sos_kernel_pri.h"

sos_kernel_t sos_k;

#define L_BIT(id)   ((uint32_t)1u << (id))     /* foreground ids (< 32) only */
#define L_DESC(id)  (&sos_k.cfg->tasks[(id)])

/* ------------------------------------------------------------------------ */
/* helpers                                                                   */
/* ------------------------------------------------------------------------ */

void sos_i_ready_set(sos_task_id_t id)
{
    sos_k.ready_bits |= L_BIT(id);
}

static bool l_is_fg(sos_task_id_t id)
{
    return id < sos_k.fg_count;
}

static bool l_is_ready(sos_task_id_t id)
{
    bool r;
    sos_platform_critical_enter();
    r = (sos_k.ready_bits & L_BIT(id)) != 0u;
    sos_platform_critical_exit();
    return r;
}

static bool l_is_periodic(sos_task_id_t id)
{
    return (L_DESC(id)->trigger & SOS_TRIG_PERIODIC) != 0u;
}

/* Lower the cached "next due" time to t, unless the cached time is already
 * due (then a scan is pending and will recompute it). */
static void l_due_update(sos_tick_t now, sos_tick_t t)
{
    if (!sos_k.next_due_valid)
    {
        sos_k.next_due = t;
        sos_k.next_due_valid = true;
    }
    else if (!sos_i_reached(now, sos_k.next_due)
             && ((sos_tick_t)(t - now) < (sos_tick_t)(sos_k.next_due - now)))
    {
        sos_k.next_due = t;
    }
    else
    {
        /* keep */
    }
}

static void l_hook_task_error(sos_task_id_t id, sos_status_t code)
{
    if ((sos_k.hooks != NULL) && (sos_k.hooks->on_task_error != NULL))
    {
        sos_k.hooks->on_task_error(id, code);
    }
}

/* Put a task into its initial runtime state (events, alarm, release). */
static void l_tcb_arm(sos_task_id_t id, sos_tick_t now)
{
    sos_tcb_t *t = &sos_k.tcb[id];

    if (l_is_fg(id))
    {
        sos_k.deferred_bits &= ~L_BIT(id);
    }
    t->flags = 0u;
    t->again_cnt = 0u;
    t->last_alive = now;
    t->next_release = (sos_tick_t)(now + t->offset);

    sos_platform_critical_enter();          /* state and events are read by async posters */
    t->events = 0u;
    if (l_is_fg(id))
    {
        sos_k.ready_bits &= ~L_BIT(id);
    }
    t->state = (uint8_t)SOS_STATE_WAITING;
    sos_platform_critical_exit();
    if (l_is_periodic(id))
    {
        l_due_update(now, t->next_release);
    }
}

static void l_stop(sos_task_id_t id)
{
    sos_tcb_t *t = &sos_k.tcb[id];

    if (t->state == (uint8_t)SOS_STATE_WAITING)
    {
        sos_platform_critical_enter();
        if (l_is_fg(id))
        {
            sos_k.ready_bits &= ~L_BIT(id);
        }
        t->events = 0u;
        t->state = (uint8_t)SOS_STATE_STOPPED;
        sos_platform_critical_exit();
        if (l_is_fg(id))
        {
            sos_k.deferred_bits &= ~L_BIT(id);
        }
        t->flags = 0u;
        if (L_DESC(id)->done != NULL)
        {
            L_DESC(id)->done(L_DESC(id)->ctx);
        }
    }
}

/* Run init until it succeeds, honouring the task's error policy. With
 * policy_restart each attempt counts against max_restart. A failed init is
 * not followed by done(): there is nothing to undo. */
static void l_start_task(sos_task_id_t id, bool policy_restart)
{
    const sos_task_desc_t *d = L_DESC(id);
    sos_tcb_t *t = &sos_k.tcb[id];

    for (;;)
    {
        if (policy_restart)
        {
            if (t->restart_cnt >= d->max_restart)
            {
                sos_fatal(SOS_E_RESTART_LIMIT);
            }
            t->restart_cnt++;
        }
        if ((d->init == NULL) || (d->init(d->ctx) == SOS_OK))
        {
            l_tcb_arm(id, sos_platform_now());
            return;
        }

        sos_i_error_log(id, SOS_E_INIT);
        l_hook_task_error(id, SOS_E_INIT);
        switch (d->on_error)
        {
            case SOS_ON_ERR_RESTART:
                policy_restart = true;          /* retry, bounded by max_restart */
                break;
            case SOS_ON_ERR_STOP:
                sos_platform_critical_enter();
                t->state = (uint8_t)SOS_STATE_STOPPED;
                sos_platform_critical_exit();
                return;
            case SOS_ON_ERR_IGNORE:
                l_tcb_arm(id, sos_platform_now());
                return;
            default:
                sos_fatal(SOS_E_INIT);
                return;
        }
    }
}

static void l_restart(sos_task_id_t id, bool by_policy)
{
    const sos_task_desc_t *d = L_DESC(id);

    if (d->done != NULL)
    {
        d->done(d->ctx);
    }
    l_start_task(id, by_policy);
}

/* After a completed step: advance the periodic release if it was reached.
 * Whole periods missed are counted and skipped, never replayed. */
static void l_advance_release(sos_task_id_t id, sos_tick_t now)
{
    sos_tcb_t *t = &sos_k.tcb[id];

    if (!l_is_periodic(id))
    {
        return;
    }
    if (sos_i_reached(now, t->next_release))
    {
        sos_tick_t k = (sos_tick_t)((sos_tick_t)(now - t->next_release) / t->period);
        t->next_release = (sos_tick_t)(t->next_release + (sos_tick_t)((k + 1u) * t->period));
        if (k != 0u)
        {
            /* widen first: no comparison on sos_tick_t, whose width varies */
            uint64_t m = (uint64_t)t->miss_cnt + (uint64_t)k;
            t->miss_cnt = (uint16_t)((m > 0xFFFFu) ? 0xFFFFu : m);
        }
    }
    l_due_update(now, t->next_release);
}

void sos_i_task_error(sos_task_id_t id, sos_status_t code, sos_tick_t now)
{
    sos_i_error_log(id, code);
    l_hook_task_error(id, code);

    switch (L_DESC(id)->on_error)
    {
        case SOS_ON_ERR_IGNORE:
            if ((code == SOS_E_TASK) || (code == SOS_E_RESULT))
            {
                l_advance_release(id, now); /* no hot loop on a failing periodic task */
            }
            break;
        case SOS_ON_ERR_RESTART:
            l_restart(id, true);
            break;
        case SOS_ON_ERR_STOP:
            l_stop(id);
            break;
        default:
            sos_fatal(code);
            break;
    }
}

/* Release time-triggered tasks whose time has come; recompute next_due. */
static void l_time_release(sos_tick_t now)
{
    uint8_t i;

    if (!sos_k.next_due_valid || !sos_i_reached(now, sos_k.next_due))
    {
        return;
    }
    sos_k.next_due_valid = false;

    for (i = 0u; i < sos_k.fg_count; i++)
    {
        sos_tcb_t *t = &sos_k.tcb[i];

        if (t->state != (uint8_t)SOS_STATE_WAITING)
        {
            continue;
        }
        if (l_is_periodic(i))
        {
            if (sos_i_reached(now, t->next_release))
            {
                sos_platform_critical_enter();
                sos_i_ready_set(i);
                sos_platform_critical_exit();
            }
            else
            {
                l_due_update(now, t->next_release);
            }
        }
        if ((t->flags & SOS_F_ALARM) != 0u)
        {
            if (sos_i_reached(now, t->alarm_at))
            {
                t->flags &= (uint8_t)~SOS_F_ALARM;
                sos_platform_critical_enter();
                t->events |= SOS_EV_ALARM;
                sos_i_ready_set(i);
                sos_platform_critical_exit();
            }
            else
            {
                l_due_update(now, t->alarm_at);
            }
        }
    }
}

/* Execute one step of task id and apply its result. */
static void l_dispatch(sos_task_id_t id)
{
    const sos_task_desc_t *d = L_DESC(id);
    sos_tcb_t *t = &sos_k.tcb[id];
    sos_events_t ev;
    sos_run_result_t r;
    sos_tick_t t0;
    sos_tick_t t1;
    sos_tick_t dt;
    bool overrun_error;

    sos_platform_critical_enter();
    if (l_is_fg(id))
    {
        sos_k.ready_bits &= ~L_BIT(id);
    }
    ev = t->events;
    t->events = 0u;
    sos_platform_critical_exit();

    sos_k.current = id;
    t0 = sos_platform_now();
    r  = d->run(d->ctx, ev);
    t1 = sos_platform_now();
    sos_k.current = SOS_TASK_NONE;

    dt = (sos_tick_t)(t1 - t0);
    if (dt > t->max_step)
    {
        t->max_step = dt;
    }
    overrun_error = sos_i_step_overrun(id, dt);
    sos_k.deferred_bits = 0u;               /* the yielded opportunity was given */

    if ((t->flags & SOS_F_STOP_REQ) != 0u)
    {
        l_stop(id);
        return;
    }
    if ((t->flags & SOS_F_RESTART_REQ) != 0u)
    {
        l_restart(id, false);
        return;
    }

    switch (r)
    {
        case SOS_RUN_OK:
            t->restart_cnt = 0u;
            t->again_cnt = 0u;
            t->last_alive = t1;
            t->flags &= (uint8_t)~SOS_F_ALIVE_FAIL;
            l_advance_release(id, t1);
            break;

        case SOS_RUN_AGAIN:
            t->restart_cnt = 0u;
            t->last_alive = t1;
            t->flags &= (uint8_t)~SOS_F_ALIVE_FAIL;
            if (l_is_fg(id))
            {
                sos_platform_critical_enter();
                sos_i_ready_set(id);
                sos_platform_critical_exit();
                if (d->again_limit != 0u)
                {
                    t->again_cnt++;
                    if (t->again_cnt >= d->again_limit)
                    {
                        sos_k.deferred_bits |= L_BIT(id);
                        t->again_cnt = 0u;
                    }
                }
            }
            break;

        case SOS_RUN_STOP:
            l_stop(id);
            break;

        case SOS_RUN_ERR:
            t->again_cnt = 0u;
            sos_i_task_error(id, SOS_E_TASK, t1);
            break;

        default:
            t->again_cnt = 0u;
            sos_i_task_error(id, SOS_E_RESULT, t1);
            break;
    }

    if (overrun_error && (t->state == (uint8_t)SOS_STATE_WAITING))
    {
        sos_i_task_error(id, SOS_E_OVERRUN, t1);
    }
}

/* Pick the next background task, round robin. Returns SOS_TASK_NONE if none. */
static sos_task_id_t l_next_background(void)
{
    uint8_t bg_count = (uint8_t)(sos_k.task_count - sos_k.fg_count);
    uint8_t n;

    for (n = 0u; n < bg_count; n++)
    {
        sos_task_id_t id = (sos_task_id_t)(sos_k.fg_count + sos_k.bg_next);
        sos_k.bg_next = (uint8_t)((sos_k.bg_next + 1u) % bg_count);
        if (sos_k.tcb[id].state == (uint8_t)SOS_STATE_WAITING)
        {
            return id;
        }
    }
    return SOS_TASK_NONE;
}

/* ------------------------------------------------------------------------ */
/* configuration check                                                       */
/* ------------------------------------------------------------------------ */

static bool l_time_ok(sos_time_t v)
{
    return sos_time_to_ticks(v) < SOS_TICK_LIMIT;
}

static sos_status_t l_check(const sos_cfg_root_t *root, uint8_t *fg_out)
{
    uint8_t i;
    uint8_t fg = 0u;
    bool seen_bg = false;

    if ((root == NULL) || (root->tasks == NULL))
    {
        return SOS_E_PARAM;
    }
    if ((root->abi_version != SOS_ABI_VERSION)
        || (root->task_count == 0u) || (root->task_count > SOS_CFG_MAX_TASKS))
    {
        return SOS_E_CFG;
    }

    for (i = 0u; i < root->task_count; i++)
    {
        const sos_task_desc_t *d = &root->tasks[i];

        if ((d->run == NULL) || (d->on_error > SOS_ON_ERR_FATAL)
            || !l_time_ok(d->period) || !l_time_ok(d->offset)
            || !l_time_ok(d->budget) || !l_time_ok(d->alive_window))
        {
            return SOS_E_CFG;
        }
        if (d->trigger == SOS_TRIG_BACKGROUND)
        {
            seen_bg = true;
            continue;
        }
        if (seen_bg                                         /* fg after bg */
            || (d->trigger == 0u)
            || ((d->trigger & (uint8_t)~(SOS_TRIG_PERIODIC | SOS_TRIG_EVENT)) != 0u)
            || ((fg != 0u) && (d->prio <= root->tasks[i - 1u].prio))
            || (((d->trigger & SOS_TRIG_PERIODIC) != 0u) && (d->period == 0u))
            || (((d->trigger & SOS_TRIG_EVENT) != 0u)
                && ((d->event_mask == 0u) || ((d->event_mask & SOS_EV_ALARM) != 0u))))
        {
            return SOS_E_CFG;
        }
        fg++;
        if (fg > SOS_FG_MAX)
        {
            return SOS_E_CFG;
        }
    }
    *fg_out = fg;
    return sos_i_ring_check(root);
}

/* ------------------------------------------------------------------------ */
/* public API                                                                */
/* ------------------------------------------------------------------------ */

sos_status_t sos_init(const sos_cfg_root_t *root)
{
    uint8_t fg = 0u;
    uint8_t i;
    sos_status_t st;

    sos_k.initialized = false;
    sos_k.current = SOS_TASK_NONE;
    sos_k.hooks = NULL;
    sos_i_error_reset();

    st = l_check(root, &fg);
    if (st != SOS_OK)
    {
        sos_i_error_log(SOS_TASK_NONE, st);
        return st;
    }

    sos_k.cfg = root;
    sos_k.hooks = root->hooks;
    sos_k.task_count = root->task_count;
    sos_k.fg_count = fg;
    sos_k.bg_next = 0u;
    sos_k.ready_bits = 0u;
    sos_k.deferred_bits = 0u;
    sos_k.next_due_valid = false;
    sos_k.ring_count = root->ring_count;
    sos_i_ring_reset();
    sos_i_supervise_reset(sos_platform_now());

    for (i = 0u; i < sos_k.task_count; i++)
    {
        sos_tcb_t *t = &sos_k.tcb[i];
        t->state = (uint8_t)SOS_STATE_DORMANT;
        t->flags = 0u;
        t->restart_cnt = 0u;
        t->again_cnt = 0u;
        t->miss_cnt = 0u;
        t->overrun_cnt = 0u;
        t->events = 0u;
        t->max_step = 0u;
        t->period = sos_time_to_ticks(root->tasks[i].period);
        t->offset = sos_time_to_ticks(root->tasks[i].offset);
        t->budget = sos_time_to_ticks(root->tasks[i].budget);
        t->alive_window = sos_time_to_ticks(root->tasks[i].alive_window);
        if ((t->budget != 0u) && (t->budget < 10u))
        {
            sos_i_error_log(i, SOS_W_PRECISION);    /* resolution > budget / 10 */
        }
    }

    sos_k.initialized = true;               /* events may be posted from init() */
    for (i = 0u; i < sos_k.task_count; i++)
    {
        l_start_task(i, false);
    }
    return SOS_OK;
}

sos_poll_result_t sos_poll(sos_tick_t *next_deadline)
{
    sos_tick_t now;
    uint32_t cand;
    sos_task_id_t bg;

    if (!sos_k.initialized)
    {
        return SOS_POLL_NOT_INIT;
    }

    now = sos_platform_now();
    l_time_release(now);
    sos_i_supervise(now);

    sos_platform_critical_enter();
    cand = sos_k.ready_bits & ~sos_k.deferred_bits;
    sos_platform_critical_exit();
    if (cand != 0u)
    {
        l_dispatch(sos_i_lowest_bit(cand));
        return SOS_POLL_RAN;
    }

    /* idle slot: background step, else the yielded task, else wait */
    bg = l_next_background();
    if (bg != SOS_TASK_NONE)
    {
        l_dispatch(bg);
        return SOS_POLL_RAN;
    }
    if (sos_k.deferred_bits != 0u)
    {
        sos_k.deferred_bits = 0u;
        sos_platform_critical_enter();
        cand = sos_k.ready_bits;
        sos_platform_critical_exit();
        if (cand != 0u)
        {
            l_dispatch(sos_i_lowest_bit(cand));
            return SOS_POLL_RAN;
        }
    }

    if (next_deadline != NULL)
    {
        sos_tick_t dl = sos_k.sup_next;     /* supervision keeps running while idle */
        if (sos_k.next_due_valid
            && ((sos_tick_t)(sos_k.next_due - now) < (sos_tick_t)(dl - now)))
        {
            dl = sos_k.next_due;
        }
        *next_deadline = dl;
    }
    return SOS_POLL_IDLE;
}

void sos_start(void)
{
    sos_tick_t deadline = 0u;

    for (;;)
    {
        sos_poll_result_t r = sos_poll(&deadline);

        if (r == SOS_POLL_IDLE)
        {
            if ((sos_k.hooks != NULL) && (sos_k.hooks->on_idle != NULL))
            {
                sos_k.hooks->on_idle();
            }
            sos_platform_wait_until(deadline);
        }
        else if (r == SOS_POLL_NOT_INIT)
        {
            sos_fatal(SOS_E_STATE);
        }
        else
        {
            /* ran a step; evaluate again */
        }
    }
}

uint32_t sos_version(void)
{
    return (SOS_VERSION_MAJOR << 16) | (SOS_VERSION_MINOR << 8) | SOS_VERSION_PATCH;
}

sos_task_id_t sos_task_self(void)
{
    return sos_k.current;
}

sos_status_t sos_task_set_alarm(sos_time_t delay)
{
    sos_task_id_t id = sos_k.current;
    sos_tick_t now;

    if ((id == SOS_TASK_NONE) || !l_is_fg(id))
    {
        return SOS_E_STATE;
    }
    if (!l_time_ok(delay))
    {
        return SOS_E_PARAM;
    }
    now = sos_platform_now();
    sos_k.tcb[id].alarm_at = (sos_tick_t)(now + sos_time_to_ticks(delay));
    sos_k.tcb[id].flags |= SOS_F_ALARM;
    l_due_update(now, sos_k.tcb[id].alarm_at);
    return SOS_OK;
}

sos_status_t sos_task_cancel_alarm(void)
{
    sos_task_id_t id = sos_k.current;

    if (id == SOS_TASK_NONE)
    {
        return SOS_E_STATE;
    }
    sos_k.tcb[id].flags &= (uint8_t)~SOS_F_ALARM;   /* a stale next_due is harmless */
    return SOS_OK;
}

sos_status_t sos_task_stop(sos_task_id_t id)
{
    if (!sos_k.initialized || (id >= sos_k.task_count))
    {
        return SOS_E_PARAM;
    }
    if (sos_k.tcb[id].state != (uint8_t)SOS_STATE_WAITING)
    {
        return SOS_E_STATE;
    }
    if (id == sos_k.current)
    {
        sos_k.tcb[id].flags |= SOS_F_STOP_REQ;      /* applied when run() returns */
    }
    else
    {
        l_stop(id);
    }
    return SOS_OK;
}

sos_status_t sos_task_restart(sos_task_id_t id)
{
    if (!sos_k.initialized || (id >= sos_k.task_count))
    {
        return SOS_E_PARAM;
    }
    if (sos_k.tcb[id].state == (uint8_t)SOS_STATE_DORMANT)
    {
        return SOS_E_STATE;
    }
    if (id == sos_k.current)
    {
        sos_k.tcb[id].flags |= SOS_F_RESTART_REQ;   /* applied when run() returns */
    }
    else if (sos_k.tcb[id].state == (uint8_t)SOS_STATE_STOPPED)
    {
        l_start_task(id, false);                    /* done() already ran on stop */
    }
    else
    {
        l_restart(id, false);
    }
    return SOS_OK;
}

sos_status_t sos_task_get_stats(sos_task_id_t id, sos_task_stats_t *out)
{
    const sos_tcb_t *t;

    if (!sos_k.initialized || (id >= sos_k.task_count) || (out == NULL))
    {
        return SOS_E_PARAM;
    }
    t = &sos_k.tcb[id];

    if (id == sos_k.current)
    {
        out->state = SOS_STATE_RUNNING;
    }
    else if (t->state != (uint8_t)SOS_STATE_WAITING)
    {
        out->state = (sos_task_state_t)t->state;
    }
    else if (!l_is_fg(id) || l_is_ready(id))
    {
        out->state = SOS_STATE_READY;               /* background tasks are always ready */
    }
    else
    {
        out->state = SOS_STATE_WAITING;
    }
    out->restart_cnt = t->restart_cnt;
    out->miss_cnt = t->miss_cnt;
    out->overrun_cnt = t->overrun_cnt;
    out->max_step = t->max_step;
    return SOS_OK;
}
