/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Hand-written configuration example on the host virtual-time platform.
 *
 * A two-stage pipeline (docs/03 section 13, without ring buffers until v0.2):
 *   ingress  (prio 0) periodic 1 ms: every 3rd tick "assembles" a request
 *                     over two AGAIN steps, then signals worker
 *   worker   (prio 1) event REQ_READY: processes the request in 4 steps,
 *                     again_limit = 2 so heartbeat can get through
 *   heartbeat(prio 2) periodic 5 ms
 *   selfcheck        background, fills idle time
 *
 * On a real target sos_start() would be called instead of the bounded loop.
 */
#include <stdio.h>
#include "sos_pub.h"
#include "host_platform.h"

enum { T_INGRESS, T_WORKER, T_HEARTBEAT, T_SELFCHECK };
#define EV_REQ_READY  0x1u

typedef struct { unsigned tick; unsigned step; } ingress_ctx_t;
typedef struct { unsigned step; unsigned done; } worker_ctx_t;
typedef struct { unsigned blocks; } selfcheck_ctx_t;

static ingress_ctx_t   g_ingress;
static worker_ctx_t    g_worker;
static selfcheck_ctx_t g_selfcheck;

static void show(const char *what)
{
    printf("%7.3f ms  %s\n", (double)host_abs_now() / 1000.0, what);
}

static sos_run_result_t ingress_run(void *ctx, sos_events_t ev)
{
    ingress_ctx_t *c = (ingress_ctx_t *)ctx;
    (void)ev;

    if (c->step == 0u)
    {
        c->tick++;
        if ((c->tick % 3u) != 1u)
        {
            return SOS_RUN_OK;                   /* nothing arrived this tick */
        }
    }
    host_advance(50u);                           /* simulated work */
    c->step++;
    if (c->step < 2u)
    {
        show("ingress  assembling");
        return SOS_RUN_AGAIN;
    }
    c->step = 0u;
    show("ingress  request ready -> worker");
    (void)sos_event_set(T_WORKER, EV_REQ_READY);
    return SOS_RUN_OK;
}

static sos_run_result_t worker_run(void *ctx, sos_events_t ev)
{
    worker_ctx_t *c = (worker_ctx_t *)ctx;
    (void)ev;

    host_advance(200u);
    c->step++;
    if (c->step < 4u)
    {
        show("worker   processing");
        return SOS_RUN_AGAIN;
    }
    c->step = 0u;
    c->done++;
    show("worker   response done");
    return SOS_RUN_OK;
}

static sos_run_result_t heartbeat_run(void *ctx, sos_events_t ev)
{
    (void)ctx;
    (void)ev;
    show("heartbeat");
    return SOS_RUN_OK;
}

static sos_run_result_t selfcheck_run(void *ctx, sos_events_t ev)
{
    (void)ev;
    ((selfcheck_ctx_t *)ctx)->blocks++;
    host_advance(100u);                          /* check one block */
    return SOS_RUN_OK;
}

/* Foreground tasks in ascending priority, then background tasks. */
static const sos_task_desc_t g_tasks[] =
{
    /* name        prio trigger                      onerr                 maxr again period offset budget alive mask          init  run            done  ctx */
    { "ingress",   0u, SOS_TRIG_PERIODIC,            SOS_ON_ERR_FATAL,     0u,  0u,  1000u, 0u,    0u,    0u,   0u,           NULL, ingress_run,   NULL, &g_ingress },
    { "worker",    1u, SOS_TRIG_EVENT,               SOS_ON_ERR_RESTART,   3u,  2u,  0u,    0u,    0u,    0u,   EV_REQ_READY, NULL, worker_run,    NULL, &g_worker },
    { "heartbeat", 2u, SOS_TRIG_PERIODIC,            SOS_ON_ERR_FATAL,     0u,  0u,  5000u, 0u,    0u,    0u,   0u,           NULL, heartbeat_run, NULL, NULL },
    { "selfcheck", 0u, SOS_TRIG_BACKGROUND,          SOS_ON_ERR_IGNORE,    0u,  0u,  0u,    0u,    0u,    0u,   0u,           NULL, selfcheck_run, NULL, &g_selfcheck },
};

static const sos_cfg_root_t g_root =
{
    SOS_ABI_VERSION, 0u /* hand-written */, 0u, g_tasks,
    (uint8_t)(sizeof g_tasks / sizeof g_tasks[0]), NULL
};

int main(void)
{
    host_reset(0u);
    if (sos_init(&g_root) != SOS_OK)
    {
        printf("sos_init failed\n");
        return 1;
    }

    while (host_abs_now() < 12000u)              /* 12 ms of virtual time */
    {
        sos_tick_t deadline;
        if (sos_poll(&deadline) == SOS_POLL_IDLE)
        {
            sos_platform_wait_until(deadline);
        }
    }

    printf("\nrequests done: %u, selfcheck blocks: %u, errors: %u\n",
           g_worker.done, g_selfcheck.blocks, (unsigned)sos_error_count());
    return 0;
}
