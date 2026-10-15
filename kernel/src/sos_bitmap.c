/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Lowest-set-bit lookup for the ready bitmap: constant time, standard C only.
 * A platform may substitute its own equivalent via SOS_CFG_FIND_FIRST_SET. */
#include "sos_kernel_pri.h"

#ifndef SOS_CFG_FIND_FIRST_SET

/* de Bruijn sequence 0x077CB531: maps an isolated bit to its index */
static const uint8_t l_debruijn[32] =
{
     0u,  1u, 28u,  2u, 29u, 14u, 24u,  3u, 30u, 22u, 20u, 15u, 25u, 17u,  4u,  8u,
    31u, 27u, 13u, 23u, 21u, 19u, 16u,  7u, 26u, 12u, 18u,  6u, 11u,  5u, 10u,  9u
};

uint8_t sos_i_lowest_bit(uint32_t x)
{
    uint32_t isolated = x & (0u - x);
    return l_debruijn[(uint32_t)(isolated * 0x077CB531u) >> 27];
}

#else

uint8_t sos_i_lowest_bit(uint32_t x)
{
    return (uint8_t)SOS_CFG_FIND_FIRST_SET(x);
}

#endif
