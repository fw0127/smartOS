/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef SOS_ERROR_H
#define SOS_ERROR_H

typedef enum
{
    SOS_OK = 0,
    SOS_E_PARAM,          /* invalid argument */
    SOS_E_STATE,          /* call not allowed in the current state */
    SOS_E_CFG,            /* configuration rejected by sos_init */
    SOS_E_FULL,           /* ring buffer full (v0.2) */
    SOS_E_EMPTY,          /* ring buffer empty (v0.2) */
    SOS_E_INIT,           /* a task's init callback failed */
    SOS_E_TASK,           /* a task's run callback returned SOS_RUN_ERR */
    SOS_E_RESULT,         /* a task's run callback returned an unknown value */
    SOS_E_RESTART_LIMIT,  /* RESTART policy exceeded max_restart */
    SOS_E_ALIVE,          /* alive supervision failed (v0.2) */
    SOS_E_OVERRUN,        /* step exceeded its budget (v0.2) */
    SOS_E_FATAL_USER      /* first code free for application use with sos_fatal */
} sos_status_t;

#endif /* SOS_ERROR_H */
