/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Kernel-internal state shared between kernel source files. Not public. */
#ifndef SOS_KERNEL_PRI_H
#define SOS_KERNEL_PRI_H

#include "sos_pub.h"

#define SOS_TICK_HALF   ((sos_tick_t)((sos_tick_t)1u << (SOS_PLATFORM_TICK_BITS - 1)))
#define SOS_TICK_LIMIT  ((sos_tick_t)(SOS_TICK_HALF / 2u))  /* max configurable interval */

/* tcb flags */
#define SOS_F_ALARM       0x01u
#define SOS_F_STOP_REQ    0x02u
#define SOS_F_RESTART_REQ 0x04u

typedef struct
{
    uint8_t               state;        /* DORMANT, WAITING (= active), STOPPED */
    uint8_t               flags;
    uint8_t               restart_cnt;
    uint16_t              again_cnt;
    uint16_t              miss_cnt;
    uint16_t              overrun_cnt;
    volatile sos_events_t events;
    sos_tick_t            period;       /* converted from the descriptor */
    sos_tick_t            offset;
    sos_tick_t            next_release;
    sos_tick_t            alarm_at;
    sos_tick_t            last_alive;
    sos_tick_t            max_step;
} sos_tcb_t;

typedef struct
{
    const sos_cfg_root_t *cfg;
    const sos_hooks_t    *hooks;
    sos_tcb_t             tcb[SOS_CFG_MAX_TASKS];
    volatile uint32_t     ready_bits;     /* bit i = foreground task i */
    uint32_t              deferred_bits;  /* tasks yielding once (AGAIN limit) */
    sos_tick_t            next_due;       /* earliest pending release / alarm */
    bool                  next_due_valid;
    bool                  initialized;
    uint8_t               task_count;
    uint8_t               fg_count;
    uint8_t               bg_next;
    sos_task_id_t         current;
    /* error log */
    uint32_t              err_total;
    sos_error_entry_t     err_log[SOS_CFG_ERRLOG_DEPTH];
} sos_kernel_t;

extern sos_kernel_t sos_k;

/* sos_time.c */
bool sos_i_reached(sos_tick_t now, sos_tick_t t);   /* now is at or after t */

/* sos_bitmap.c */
uint8_t sos_i_lowest_bit(uint32_t x);               /* x != 0 */

/* sos_error.c */
void sos_i_error_log(sos_task_id_t task, sos_status_t code);
void sos_i_error_reset(void);

/* sos_event.c */
sos_status_t sos_i_event_post(sos_task_id_t id, sos_events_t bits);

/* sos_sched.c */
void sos_i_ready_set(sos_task_id_t id);             /* caller holds critical section */

#endif /* SOS_KERNEL_PRI_H */
