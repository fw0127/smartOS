/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Error log ring and the single fatal exit. */
#include "sos_kernel_pri.h"

void sos_i_error_reset(void)
{
    sos_k.err_total = 0u;
}

void sos_i_error_log(sos_task_id_t task, sos_status_t code)
{
    sos_error_entry_t *e = &sos_k.err_log[sos_k.err_total % SOS_CFG_ERRLOG_DEPTH];

    e->when = sos_platform_now();
    e->task = task;
    e->code = code;
    if (sos_k.err_total != 0xFFFFFFFFu)
    {
        sos_k.err_total++;
    }
}

uint32_t sos_error_count(void)
{
    return sos_k.err_total;
}

sos_status_t sos_error_get(uint32_t i, sos_error_entry_t *out)
{
    uint32_t kept = (sos_k.err_total < SOS_CFG_ERRLOG_DEPTH) ? sos_k.err_total
                                                              : SOS_CFG_ERRLOG_DEPTH;
    if ((out == NULL) || (i >= kept))
    {
        return SOS_E_PARAM;
    }
    *out = sos_k.err_log[(sos_k.err_total - kept + i) % SOS_CFG_ERRLOG_DEPTH];
    return SOS_OK;
}

void sos_fatal(sos_status_t code)
{
    sos_i_error_log(sos_k.current, code);
    if ((sos_k.hooks != NULL) && (sos_k.hooks->on_fatal != NULL))
    {
        sos_k.hooks->on_fatal(code);
    }
    sos_platform_halt((uint32_t)code);
    for (;;)
    {
        /* platform contract violated: halt returned */
    }
}
