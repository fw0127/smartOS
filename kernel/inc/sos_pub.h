/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* smartOs public interface. Applications include only this header. */
#ifndef SOS_PUB_H
#define SOS_PUB_H

#include "sos_types.h"

/* ---- lifecycle ----------------------------------------------------------- */
sos_status_t      sos_init(const sos_cfg_root_t *root);
void              sos_start(void);                        /* does not return */
sos_poll_result_t sos_poll(sos_tick_t *next_deadline);    /* one iteration, never waits */
void              sos_fatal(sos_status_t code);           /* does not return */
uint32_t          sos_version(void);                      /* 0x00MMmmpp */

/* ---- task control (task context) ----------------------------------------- */
sos_task_id_t sos_task_self(void);
sos_status_t  sos_task_set_alarm(sos_time_t delay);
sos_status_t  sos_task_cancel_alarm(void);
sos_status_t  sos_task_stop(sos_task_id_t id);
sos_status_t  sos_task_restart(sos_task_id_t id);
sos_status_t  sos_task_get_stats(sos_task_id_t id, sos_task_stats_t *out);

/* ---- events -------------------------------------------------------------- */
sos_status_t sos_event_set(sos_task_id_t id, sos_events_t bits);
sos_status_t sos_event_set_async(sos_task_id_t id, sos_events_t bits);
sos_events_t sos_event_peek(sos_task_id_t id);

/* ---- ring buffers (one producer, one consumer) ---------------------------- */
sos_status_t sos_ring_put(sos_ring_t r, const void *elem);   /* producer, async-safe */
sos_status_t sos_ring_get(sos_ring_t r, void *out);          /* consumer */
sos_status_t sos_ring_peek(sos_ring_t r, void *out);         /* consumer */
void        *sos_ring_reserve(sos_ring_t r);                 /* producer, async-safe */
sos_status_t sos_ring_commit(sos_ring_t r);                  /* producer, async-safe */
const void  *sos_ring_acquire(sos_ring_t r);                 /* consumer */
sos_status_t sos_ring_release(sos_ring_t r);                 /* consumer */
uint16_t     sos_ring_count(sos_ring_t r);
sos_status_t sos_ring_get_stats(sos_ring_t r, sos_ring_stats_t *out);

/* ---- time ---------------------------------------------------------------- */
sos_tick_t sos_time_now(void);
sos_tick_t sos_time_to_ticks(sos_time_t t);
sos_time_t sos_ticks_to_time(sos_tick_t k);
sos_tick_t sos_time_since(sos_tick_t start);
bool       sos_time_elapsed(sos_tick_t start, sos_tick_t dur);

/* ---- critical section ---------------------------------------------------- */
void sos_critical_enter(void);
void sos_critical_exit(void);

/* ---- diagnostics --------------------------------------------------------- */
uint32_t     sos_error_count(void);   /* total logged since sos_init */
sos_status_t sos_error_get(uint32_t i, sos_error_entry_t *out); /* 0 = oldest kept */

#endif /* SOS_PUB_H */
