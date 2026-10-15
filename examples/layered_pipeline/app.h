/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Application types and task contexts referenced by app.toml. */
#ifndef APP_H
#define APP_H

#include <stdint.h>

/* one "bus frame": requests arrive split into frames */
typedef struct
{
    uint8_t req_id;
    uint8_t last;           /* 1 on the final frame of a request */
    uint8_t len;
    uint8_t data[5];
} app_frame_t;

/* an assembled request */
typedef struct
{
    uint16_t id;
    uint8_t  len;
    uint8_t  payload[32];
} app_request_t;

typedef struct
{
    app_request_t cur;      /* request being assembled */
    uint32_t      requests; /* assembled and queued */
    uint32_t      responses;
} ingress_ctx_t;

typedef struct
{
    uint8_t  step;          /* processing step of the held request */
    uint32_t done;
    uint32_t checksum;      /* of the last request */
} worker_ctx_t;

typedef struct
{
    uint32_t blocks;
} selfcheck_ctx_t;

extern ingress_ctx_t   g_ingress;
extern worker_ctx_t    g_worker;
extern selfcheck_ctx_t g_selfcheck;
extern uint32_t        g_ring_overflows;

#endif
