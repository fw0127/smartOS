/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Platform contract conformance suite (docs/05 sections 1 and 2).
 *
 * The suite only calls the seven contract functions. A platform adaptation
 * runs it by linking sos_contract_test.c with a small harness that
 * implements the functions below, and calling sos_contract_run().
 *
 * sos_platform_halt is not exercised: it must not return, so it can only be
 * checked on the target (e.g. that the watchdog resets the device).
 */
#ifndef SOS_CONTRACT_TEST_H
#define SOS_CONTRACT_TEST_H

#include <stdbool.h>
#include "sos_platform.h"

/* ---- harness, provided by the platform adaptation ---------------------- */

/* Prepare the platform for one test (e.g. reset virtual time near the wrap). */
void sos_ct_setup(void);

/* Let at least 'ticks' of time pass without waiting through the contract:
 * advance virtual time, or busy-wait on a real clock. */
void sos_ct_let_time_pass(sos_tick_t ticks);

/* Optional asynchronous context. If unavailable, the tests that need one
 * are reported as skipped. */
bool sos_ct_async_available(void);
void sos_ct_async_start(void (*fn)(void *arg), void *arg);  /* returns at once */
void sos_ct_async_join(void);                               /* waits for fn */

/* Write one line of output. */
void sos_ct_print(const char *line);

/* ---- suite --------------------------------------------------------------- */

/* Runs all checks; returns the number of failed checks. */
int sos_contract_run(void);

#endif
