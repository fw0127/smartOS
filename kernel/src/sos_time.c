/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Abstract time: wrap-safe comparison and unit conversion. */
#include "sos_kernel_pri.h"

#if (SOS_PLATFORM_TICKS_PER_SEC) > 0xFFFFFFFFu
#error "SOS_PLATFORM_TICKS_PER_SEC must fit in 32 bits"
#endif

#define L_TICK_MAX ((sos_tick_t)~(sos_tick_t)0u)

bool sos_i_reached(sos_tick_t now, sos_tick_t t)
{
    /* Defined for unsigned types of any width: distance from t to now is
     * below half the range exactly when now is at or after t. */
    return (sos_tick_t)(now - t) < SOS_TICK_HALF;
}

sos_tick_t sos_time_now(void)
{
    return sos_platform_now();
}

sos_tick_t sos_time_to_ticks(sos_time_t t)
{
    uint64_t v = ((uint64_t)t * (uint64_t)(SOS_PLATFORM_TICKS_PER_SEC)
                  + (uint64_t)(SOS_TIME_UNITS_PER_SEC / 2u))
                 / (uint64_t)(SOS_TIME_UNITS_PER_SEC);

    if ((t != 0u) && (v == 0u))
    {
        v = 1u;                         /* a non-zero interval never collapses to 0 */
    }
#if SOS_PLATFORM_TICK_BITS < 64
    if (v > (uint64_t)L_TICK_MAX)
    {
        v = (uint64_t)L_TICK_MAX;       /* saturate; sos_init rejects it as out of range */
    }
#endif
    return (sos_tick_t)v;
}

sos_time_t sos_ticks_to_time(sos_tick_t k)
{
    /* split to avoid overflow with 64-bit ticks */
    uint64_t whole = (uint64_t)k / (uint64_t)(SOS_PLATFORM_TICKS_PER_SEC);
    uint64_t rest  = (uint64_t)k % (uint64_t)(SOS_PLATFORM_TICKS_PER_SEC);
    uint64_t v     = whole * (uint64_t)(SOS_TIME_UNITS_PER_SEC)
                     + (rest * (uint64_t)(SOS_TIME_UNITS_PER_SEC))
                       / (uint64_t)(SOS_PLATFORM_TICKS_PER_SEC);

    return (v > 0xFFFFFFFFu) ? (sos_time_t)0xFFFFFFFFu : (sos_time_t)v;
}

sos_tick_t sos_time_since(sos_tick_t start)
{
    return (sos_tick_t)(sos_platform_now() - start);
}

bool sos_time_elapsed(sos_tick_t start, sos_tick_t dur)
{
    return sos_time_since(start) >= dur;
}

void sos_critical_enter(void)
{
    sos_platform_critical_enter();
}

void sos_critical_exit(void)
{
    sos_platform_critical_exit();
}
