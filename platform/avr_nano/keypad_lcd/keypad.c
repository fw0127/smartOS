/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <avr/io.h>
#include <util/delay.h>
#include "keypad.h"

#define ROW_MASK   0xF0u                 /* PD4..PD7 */
#define COL_MASK   0x0Fu                 /* PB0..PB3 */
#define STABLE_N   3u

static const char l_map[4][4] =
{
    { '1', '2', '3', 'A' },
    { '4', '5', '6', 'B' },
    { '7', '8', '9', 'C' },
    { '*', '0', '#', 'D' },
};

static char    l_cand = KEY_NONE;
static uint8_t l_count;
static char    l_stable = KEY_NONE;

void keypad_init(void)
{
    DDRD  &= (uint8_t)~ROW_MASK;          /* rows released (high-Z) */
    PORTD &= (uint8_t)~ROW_MASK;          /* output value 0 once driven */
    DDRB  &= (uint8_t)~COL_MASK;
    PORTB |= COL_MASK;                    /* column pull-ups */
}

/* One pass over the matrix; the first closed key wins. Only the selected
 * row is driven (low); the others float, so two keys pressed in one
 * column cannot short two driven outputs. */
static char l_raw(void)
{
    char key = KEY_NONE;
    uint8_t r;

    for (r = 0u; (r < 4u) && (key == KEY_NONE); r++)
    {
        uint8_t row = (uint8_t)_BV(PD4 + r);
        uint8_t cols;
        DDRD |= row;
        _delay_us(5);                     /* let the line settle */
        cols = (uint8_t)(~PINB & COL_MASK);
        DDRD &= (uint8_t)~row;
        if (cols != 0u)
        {
            uint8_t c = 0u;
            while ((cols & 1u) == 0u) { cols >>= 1; c++; }
            key = l_map[r][c];
        }
    }
    return key;
}

char keypad_scan(void)
{
    char raw = l_raw();

    if (raw != l_cand)
    {
        l_cand = raw;
        l_count = 1u;
        return KEY_NONE;
    }
    if (l_count < STABLE_N)
    {
        l_count++;
        if ((l_count == STABLE_N) && (l_cand != l_stable))
        {
            l_stable = l_cand;
            return l_stable;              /* KEY_NONE on release */
        }
    }
    return KEY_NONE;
}
