/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Pipeline example driver on the host virtual-time platform. Between kernel
 * polls it plays the role of an interrupt: every 400 us a frame "arrives"
 * and is put into rx_ring; three frames form one request.
 */
#include <stdio.h>
#include "app.h"
#include "sos_cfg_gen.h"
#include "host_platform.h"

#define RUN_US        100000u
#define FRAME_EVERY   400u
#define LAST_FRAME_AT 90000u        /* leave time for the pipeline to drain */

int main(void)
{
    uint64_t next_frame = FRAME_EVERY;
    uint32_t frame_no = 0u;
    uint32_t sent = 0u;
    sos_task_stats_t st;
    sos_task_id_t id;

    host_reset(0u);
    if (SOS_CFG_INIT() != SOS_OK)                /* sos_init + header/table check */
    {
        printf("sos_init failed\n");
        return 1;
    }

    while (host_abs_now() < RUN_US)
    {
        sos_tick_t dl;

        if ((host_abs_now() >= next_frame) && (next_frame <= LAST_FRAME_AT))
        {
            app_frame_t f;
            f.req_id = (uint8_t)(frame_no / 3u);
            f.last = (uint8_t)((frame_no % 3u) == 2u);
            f.len = 5u;
            f.data[0] = (uint8_t)frame_no;
            f.data[1] = 1u; f.data[2] = 2u; f.data[3] = 3u; f.data[4] = 4u;
            (void)sos_ring_put(SOS_RING_RX_RING, &f);    /* "interrupt" */
            if (f.last != 0u) { sent++; }
            frame_no++;
            next_frame += FRAME_EVERY;
        }

        if (sos_poll(&dl) == SOS_POLL_IDLE)
        {
            uint64_t wake = host_abs_now() + (uint64_t)(sos_tick_t)(dl - sos_platform_now());
            if ((next_frame <= LAST_FRAME_AT) && (next_frame < wake))
            {
                host_advance(next_frame - host_abs_now());   /* the interrupt comes first */
            }
            else
            {
                sos_platform_wait_until(dl);
            }
        }
    }

    printf("configuration digest 0x%08X (sosgen 0x%04X)\n",
           (unsigned)sos_cfg_root.digest, (unsigned)sos_cfg_root.gen_version);
    printf("requests sent %u, assembled %u, processed %u, ring overflows %u\n",
           (unsigned)sent, (unsigned)g_ingress.requests, (unsigned)g_worker.done,
           (unsigned)g_ring_overflows);
    printf("selfcheck blocks %u, health signals %u, kernel errors %u\n",
           (unsigned)g_selfcheck.blocks, (unsigned)host_health_calls(),
           (unsigned)sos_error_count());
    for (id = 0u; id < sos_cfg_root.task_count; id++)
    {
        (void)sos_task_get_stats(id, &st);
        printf("  %-10s max step %3u us, overruns %u, missed periods %u\n",
               sos_cfg_root.tasks[id].name, (unsigned)st.max_step,
               (unsigned)st.overrun_cnt, (unsigned)st.miss_cnt);
    }
    return ((sent == g_worker.done) && (sos_error_count() == 0u)) ? 0 : 1;
}
