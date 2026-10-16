/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Per-task event bits. Setting a bit that matches the task's mask makes it
 * ready; the bits are handed to run() and cleared at dispatch. */
#include "sos_kernel_pri.h"

sos_status_t sos_i_event_post(sos_task_id_t id, sos_events_t bits)
{
    sos_status_t st = SOS_OK;

    if ((!sos_k.initialized) || (id >= sos_k.task_count) || ((bits & SOS_EV_ALARM) != 0u))
    {
        return SOS_E_PARAM;
    }

    sos_platform_critical_enter();
    if (sos_k.tcb[id].state == (uint8_t)SOS_STATE_DORMANT)
    {
        /* during sos_init, before this task's init has completed: keep the
         * bits; they are delivered when the task starts (F4) */
        sos_k.tcb[id].events |= bits;
    }
    else if (sos_k.tcb[id].state != (uint8_t)SOS_STATE_WAITING)
    {
        st = SOS_E_STATE;                   /* stopped */
    }
    else
    {
        sos_k.tcb[id].events |= bits;
        /* only EVENT-triggered tasks are woken; others get the bits with
         * their next step */
        if ((id < sos_k.fg_count)
            && ((sos_k.cfg->tasks[id].trigger & SOS_TRIG_EVENT) != 0u)
            && ((sos_k.tcb[id].events & sos_k.cfg->tasks[id].event_mask) != 0u))
        {
            sos_i_ready_set(id);
        }
    }
    sos_platform_critical_exit();
    return st;
}

sos_status_t sos_event_set(sos_task_id_t id, sos_events_t bits)
{
    return sos_i_event_post(id, bits);
}

sos_status_t sos_event_set_async(sos_task_id_t id, sos_events_t bits)
{
    sos_status_t st = sos_i_event_post(id, bits);

    if (st == SOS_OK)
    {
        sos_platform_notify();
    }
    return st;
}

sos_events_t sos_event_peek(sos_task_id_t id)
{
    sos_events_t ev = 0u;

    if (sos_k.initialized && (id < sos_k.task_count))
    {
        sos_platform_critical_enter();
        ev = sos_k.tcb[id].events;
        sos_platform_critical_exit();
    }
    return ev;
}
