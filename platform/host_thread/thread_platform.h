/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Host thread platform (POSIX). The scheduler runs on one thread; any other
 * thread acts as an asynchronous context (like an interrupt).
 *  - critical section: one recursive mutex (excludes the other threads)
 *  - wait_until: condition variable with a sticky notify flag
 *  - time: CLOCK_MONOTONIC in microseconds
 */
#ifndef THREAD_PLATFORM_H
#define THREAD_PLATFORM_H

#include <stdint.h>
#include "sos_platform.h"

void     thread_platform_init(void);    /* call once before sos_init */
uint32_t thread_platform_health_calls(void);

#endif
