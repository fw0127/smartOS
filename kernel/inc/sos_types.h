/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef SOS_TYPES_H
#define SOS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "sos_platform.h"
#include "sos_error.h"

/* ---- kernel compile-time parameters (user supplies sos_cfg.h) ---------- */
#include "sos_cfg.h"

#ifndef SOS_TIME_UNITS_PER_SEC
#define SOS_TIME_UNITS_PER_SEC      1000000u    /* 1 unit = 1 us */
#endif
#ifndef SOS_CFG_MAX_TASKS
#define SOS_CFG_MAX_TASKS           32u
#endif
#ifndef SOS_CFG_ERRLOG_DEPTH
#define SOS_CFG_ERRLOG_DEPTH        8u
#endif
#ifndef SOS_CFG_MAX_RINGS
#define SOS_CFG_MAX_RINGS           8u
#endif
#ifndef SOS_CFG_OVERRUN_IS_ERROR
#define SOS_CFG_OVERRUN_IS_ERROR    0
#endif
#ifndef SOS_CFG_HEALTH_ENABLE
#define SOS_CFG_HEALTH_ENABLE       1
#endif
#ifndef SOS_CFG_HEALTH_MIN_INTERVAL
#define SOS_CFG_HEALTH_MIN_INTERVAL 10000u      /* configuration time units */
#endif

#if (SOS_CFG_MAX_TASKS < 1) || (SOS_CFG_MAX_TASKS > 254)
#error "SOS_CFG_MAX_TASKS must be in 1..254"
#endif
#if SOS_CFG_ERRLOG_DEPTH < 1
#error "SOS_CFG_ERRLOG_DEPTH must be >= 1"
#endif
#if SOS_CFG_MAX_RINGS > 254
#error "SOS_CFG_MAX_RINGS must be <= 254"
#endif
#if SOS_CFG_HEALTH_MIN_INTERVAL < 1
#error "SOS_CFG_HEALTH_MIN_INTERVAL must be >= 1"
#endif

/* ---- versions ------------------------------------------------------------ */
#define SOS_VERSION_MAJOR   0u
#define SOS_VERSION_MINOR   2u
#define SOS_VERSION_PATCH   0u
#define SOS_ABI_VERSION     2u      /* layout of sos_cfg_root_t and friends */

/* ---- basic types --------------------------------------------------------- */
typedef uint32_t sos_time_t;        /* configuration time, SOS_TIME_UNITS_PER_SEC */
typedef uint32_t sos_events_t;
typedef uint8_t  sos_task_id_t;
typedef uint8_t  sos_ring_t;        /* ring buffer id: index in the ring table */

#define SOS_EV_ALARM        ((sos_events_t)0x80000000u)
#define SOS_TASK_NONE       ((sos_task_id_t)0xFFu)
#define SOS_RING_MAX_CAP    32768u  /* power of two, 16-bit free-running indices */
#define SOS_FG_MAX          32u     /* foreground tasks: one 32-bit ready bitmap */

typedef enum
{
    SOS_RUN_OK = 0,
    SOS_RUN_AGAIN,
    SOS_RUN_STOP,
    SOS_RUN_ERR
} sos_run_result_t;

/* trigger bits */
#define SOS_TRIG_PERIODIC   0x01u
#define SOS_TRIG_EVENT      0x02u
#define SOS_TRIG_BACKGROUND 0x04u

/* error policies */
#define SOS_ON_ERR_IGNORE   0u
#define SOS_ON_ERR_RESTART  1u
#define SOS_ON_ERR_STOP     2u
#define SOS_ON_ERR_FATAL    3u

typedef enum
{
    SOS_STATE_DORMANT = 0,
    SOS_STATE_WAITING,
    SOS_STATE_READY,
    SOS_STATE_RUNNING,
    SOS_STATE_STOPPED
} sos_task_state_t;

/* ---- configuration (read-only, produced by sosgen or written by hand) --- */
typedef struct
{
    const char        *name;
    uint8_t            prio;          /* foreground: strictly ascending in table */
    uint8_t            trigger;       /* SOS_TRIG_* */
    uint8_t            on_error;      /* SOS_ON_ERR_* */
    uint8_t            max_restart;
    uint16_t           again_limit;   /* 0 = unlimited */
    sos_time_t         period;
    sos_time_t         offset;
    sos_time_t         budget;        /* 0 = unchecked (enforced from v0.2) */
    sos_time_t         alive_window;  /* 0 = unchecked (enforced from v0.2) */
    sos_events_t       event_mask;
    sos_status_t     (*init)(void *ctx);
    sos_run_result_t (*run)(void *ctx, sos_events_t events);
    void             (*done)(void *ctx);
    void              *ctx;
} sos_task_desc_t;

typedef struct
{
    void (*on_overrun)(sos_task_id_t id, sos_tick_t step);
    void (*on_task_error)(sos_task_id_t id, sos_status_t code);
    void (*on_fatal)(sos_status_t code);
    void (*on_idle)(void);
    void (*on_ring_overflow)(uint8_t ring);
} sos_hooks_t;

/* Ring buffer: one producer (task or asynchronous context), one consumer task.
 * Storage is elem_size * capacity bytes, capacity a power of two. */
typedef struct
{
    const char   *name;
    void         *storage;
    uint16_t      elem_size;
    uint16_t      capacity;
    sos_task_id_t consumer;       /* task to notify, SOS_TASK_NONE = none */
    sos_events_t  notify;         /* event set on the consumer per publish, 0 = none */
} sos_ring_desc_t;

typedef struct
{
    uint16_t               abi_version;   /* must equal SOS_ABI_VERSION */
    uint16_t               gen_version;   /* 0 = hand-written */
    uint32_t               digest;        /* configuration digest, informative */
    const sos_task_desc_t *tasks;         /* foreground (by prio), then background */
    uint8_t                task_count;
    const sos_ring_desc_t *rings;         /* may be NULL when ring_count == 0 */
    uint8_t                ring_count;
    const sos_hooks_t     *hooks;         /* may be NULL */
} sos_cfg_root_t;

/* ---- diagnostics --------------------------------------------------------- */
typedef struct
{
    sos_task_state_t state;
    uint8_t          restart_cnt;
    uint16_t         miss_cnt;
    uint16_t         overrun_cnt;
    sos_tick_t       max_step;
} sos_task_stats_t;

typedef struct
{
    uint16_t capacity;
    uint16_t count;
    uint16_t high_water;
    uint16_t overflow_cnt;
} sos_ring_stats_t;

typedef struct
{
    sos_tick_t    when;
    sos_task_id_t task;        /* SOS_TASK_NONE if not task related */
    sos_status_t  code;
} sos_error_entry_t;

typedef enum
{
    SOS_POLL_RAN = 0,   /* one step of a task was executed */
    SOS_POLL_IDLE,      /* nothing to do before *next_deadline */
    SOS_POLL_NOT_INIT   /* sos_init has not succeeded */
} sos_poll_result_t;

#endif /* SOS_TYPES_H */
