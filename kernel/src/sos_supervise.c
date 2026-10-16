/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Supervision (docs/03 section 8): step budgets, alive windows and the
 * health signal. The periodic check runs at most once per
 * SOS_CFG_HEALTH_MIN_INTERVAL; the health signal is given only when no
 * supervised task is in alive failure.
 */
#include "sos_kernel_pri.h"

#if SOS_CFG_SUPERVISE_ENABLE

void sos_i_supervise_reset(sos_tick_t now)
{
    sos_k.sup_interval = sos_time_to_ticks((sos_time_t)SOS_CFG_HEALTH_MIN_INTERVAL);
    sos_k.sup_next = now;
}

bool sos_i_step_overrun(sos_task_id_t id, sos_tick_t dt)
{
    sos_tcb_t *t = &sos_k.tcb[id];

    if ((t->budget == 0u) || (dt <= t->budget))
    {
        return false;
    }
    if (t->overrun_cnt != 0xFFFFu)
    {
        t->overrun_cnt++;
    }
    if ((sos_k.hooks != NULL) && (sos_k.hooks->on_overrun != NULL))
    {
        sos_k.hooks->on_overrun(id, dt);
    }
    return SOS_CFG_OVERRUN_IS_ERROR != 0;
}

void sos_i_supervise(sos_tick_t now)
{
    bool healthy = true;
    uint8_t i;

    if (!sos_i_reached(now, sos_k.sup_next))
    {
        return;
    }
    sos_k.sup_next = (sos_tick_t)(now + sos_k.sup_interval);

    for (i = 0u; i < sos_k.task_count; i++)
    {
        sos_tcb_t *t = &sos_k.tcb[i];

        if ((t->state != (uint8_t)SOS_STATE_WAITING) || (t->alive_window == 0u))
        {
            continue;
        }
        if ((t->flags & SOS_F_ALIVE_FAIL) != 0u)
        {
            healthy = false;                    /* still failing (IGNORE policy) */
        }
        else if ((sos_tick_t)(now - t->last_alive) > t->alive_window)
        {
            healthy = false;
            t->flags |= SOS_F_ALIVE_FAIL;       /* cleared by the next completed step */
            sos_i_task_error(i, SOS_E_ALIVE, now);
        }
        else
        {
            /* alive */
        }
    }

#if SOS_CFG_HEALTH_ENABLE
    if (healthy)
    {
        sos_platform_health_signal();
    }
#else
    (void)healthy;
#endif
}

bool sos_i_supervise_deadline(sos_tick_t now, sos_tick_t *dl)
{
    (void)now;
    *dl = sos_k.sup_next;
    return true;
}

#else /* !SOS_CFG_SUPERVISE_ENABLE: no budgets, alive windows or health signal */

void sos_i_supervise_reset(sos_tick_t now)
{
    (void)now;
}

void sos_i_supervise(sos_tick_t now)
{
    (void)now;
}

bool sos_i_step_overrun(sos_task_id_t id, sos_tick_t dt)
{
    (void)id;
    (void)dt;
    return false;
}

bool sos_i_supervise_deadline(sos_tick_t now, sos_tick_t *dl)
{
    (void)now;
    (void)dl;
    return false;
}

#endif
