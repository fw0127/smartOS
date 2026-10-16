/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* 4x4 membrane keypad. Ribbon pins 1..8 (keys facing you, left to right)
 * = rows R1..R4, columns C1..C4, wired to Nano D4..D11:
 *
 *   rows    R1..R4 -> D4..D7  (PD4..PD7), driven low one at a time
 *   columns C1..C4 -> D8..D11 (PB0..PB3), inputs with pull-ups
 *
 * keypad_scan() takes a few microseconds; call it periodically (10 ms) and
 * it debounces: a key counts once it reads the same for 3 calls in a row. */
#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

#define KEY_NONE  '\0'

void keypad_init(void);
char keypad_scan(void);           /* newly pressed key, or KEY_NONE */

#endif
