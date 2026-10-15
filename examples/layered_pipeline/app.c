/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Task code for the pipeline example. Execution time is simulated with
 * host_advance() so the virtual-time run shows realistic interleaving.
 */
#include <stdio.h>
#include "app.h"
#include "sos_cfg_gen.h"
#include "host_platform.h"

ingress_ctx_t   g_ingress;
worker_ctx_t    g_worker;
selfcheck_ctx_t g_selfcheck;
uint32_t        g_ring_overflows;

#define FRAMES_PER_STEP 4u

sos_status_t ingress_init(void *ctx)
{
    ingress_ctx_t *c = (ingress_ctx_t *)ctx;
    c->cur.len = 0u;
    return SOS_OK;
}

/* Assemble frames into requests; hand complete requests to the worker
 * through req_ring, written in place (reserve/commit). */
sos_run_result_t ingress_run(void *ctx, sos_events_t ev)
{
    ingress_ctx_t *c = (ingress_ctx_t *)ctx;
    app_frame_t f;
    uint32_t n;

    if ((ev & SOS_EV_INGRESS_RESP_READY) != 0u)
    {
        c->responses = g_worker.done;
    }

    for (n = 0u; n < FRAMES_PER_STEP; n++)
    {
        app_request_t *slot = NULL;

        if (sos_ring_peek(SOS_RING_RX_RING, &f) != SOS_OK)
        {
            return SOS_RUN_OK;                      /* drained */
        }
        if (f.last != 0u)
        {
            slot = (app_request_t *)sos_ring_reserve(SOS_RING_REQ_RING);
            if (slot == NULL)
            {
                return SOS_RUN_OK;                  /* back-pressure: retry next period */
            }
        }
        (void)sos_ring_get(SOS_RING_RX_RING, &f);
        host_advance(20u);
        if ((uint32_t)c->cur.len + f.len <= sizeof c->cur.payload)
        {
            uint8_t i;
            for (i = 0u; i < f.len; i++)
            {
                c->cur.payload[c->cur.len++] = f.data[i];
            }
        }
        if (slot != NULL)
        {
            *slot = c->cur;                         /* written in place */
            slot->id = f.req_id;
            (void)sos_ring_commit(SOS_RING_REQ_RING);   /* wakes the worker */
            c->requests++;
            c->cur.len = 0u;
        }
    }
    return (sos_ring_count(SOS_RING_RX_RING) != 0u) ? SOS_RUN_AGAIN : SOS_RUN_OK;
}

/* Process one request in three steps, holding it in place across steps
 * (acquire ... AGAIN ... release). */
sos_run_result_t worker_run(void *ctx, sos_events_t ev)
{
    worker_ctx_t *c = (worker_ctx_t *)ctx;
    const app_request_t *r = (const app_request_t *)sos_ring_acquire(SOS_RING_REQ_RING);
    (void)ev;

    if (r == NULL)
    {
        return SOS_RUN_OK;
    }
    host_advance(150u);
    c->step++;
    if (c->step < 3u)
    {
        return SOS_RUN_AGAIN;
    }
    {
        uint32_t sum = r->id;
        uint8_t i;
        for (i = 0u; i < r->len; i++)
        {
            sum = sum * 31u + r->payload[i];
        }
        c->checksum = sum;
    }
    c->step = 0u;
    c->done++;
    (void)sos_ring_release(SOS_RING_REQ_RING);
    (void)sos_event_set(SOS_TASK_INGRESS, SOS_EV_INGRESS_RESP_READY);
    return (sos_ring_count(SOS_RING_REQ_RING) != 0u) ? SOS_RUN_AGAIN : SOS_RUN_OK;
}

sos_run_result_t heartbeat_run(void *ctx, sos_events_t ev)
{
    (void)ctx;
    (void)ev;
    host_advance(5u);
    return SOS_RUN_OK;
}

sos_run_result_t selfcheck_run(void *ctx, sos_events_t ev)
{
    (void)ev;
    ((selfcheck_ctx_t *)ctx)->blocks++;
    host_advance(50u);
    return SOS_RUN_OK;
}

void app_on_fatal(sos_status_t code)
{
    printf("FATAL %d\n", (int)code);
}

void app_on_ring_overflow(uint8_t ring)
{
    (void)ring;
    g_ring_overflows++;
}
