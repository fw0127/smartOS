/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Common backpack wiring: P0 = RS, P1 = RW, P2 = EN, P3 = backlight,
 * P4..P7 = D4..D7. Each byte goes out as two nibbles in one I2C
 * transaction, every nibble latched by an EN high -> low pulse. */
#include <util/delay.h>
#include "lcd1602.h"
#include "twi.h"

#define B_RS   0x01u
#define B_EN   0x04u
#define B_BL   0x08u

#define CELLS  (LCD_COLS * LCD_ROWS)
#define NOPOS  0xFFu

static uint8_t l_addr;
static uint8_t l_bl = B_BL;
static bool    l_bl_dirty;
static char    l_want[CELLS];             /* what the screen should show */
static char    l_have[CELLS];             /* what it shows */
static uint8_t l_cursor = NOPOS;          /* DDRAM cell the next write lands on */
static uint8_t l_scan;                    /* round-robin start of the diff search */

static bool l_nibble(uint8_t nib, uint8_t flags)
{
    uint8_t b[2];
    b[0] = (uint8_t)((nib << 4) | flags | l_bl | B_EN);
    b[1] = (uint8_t)((nib << 4) | flags | l_bl);
    return twi_write(l_addr, b, 2u);
}

static bool l_byte(uint8_t v, uint8_t flags)
{
    uint8_t b[4];
    b[0] = (uint8_t)((v & 0xF0u) | flags | l_bl | B_EN);
    b[1] = (uint8_t)((v & 0xF0u) | flags | l_bl);
    b[2] = (uint8_t)((uint8_t)(v << 4) | flags | l_bl | B_EN);
    b[3] = (uint8_t)((uint8_t)(v << 4) | flags | l_bl);
    return twi_write(l_addr, b, 4u);       /* > 37 us per I2C byte: no busy poll needed */
}

static bool l_cmd(uint8_t c)  { return l_byte(c, 0u); }
static bool l_data(uint8_t d) { return l_byte(d, B_RS); }

bool lcd_init(void)
{
    uint8_t i;
    bool ok;

    twi_init();
    l_addr = 0u;
    if (twi_probe(0x27u))      { l_addr = 0x27u; }
    else if (twi_probe(0x3Fu)) { l_addr = 0x3Fu; }
    else                       { return false; }

    _delay_ms(50);                            /* power-up time of the controller */
    ok = l_nibble(0x3u, 0u);                  /* 8-bit mode three times, then 4-bit */
    _delay_ms(5);
    ok = ok && l_nibble(0x3u, 0u);
    _delay_ms(5);
    ok = ok && l_nibble(0x3u, 0u);
    _delay_us(200);
    ok = ok && l_nibble(0x2u, 0u);
    ok = ok && l_cmd(0x28u);                  /* 4-bit, 2 lines, 5x8 */
    ok = ok && l_cmd(0x0Cu);                  /* display on, no cursor */
    ok = ok && l_cmd(0x06u);                  /* increment, no shift */
    ok = ok && l_cmd(0x01u);                  /* clear */
    _delay_ms(2);
    for (i = 0u; i < CELLS; i++)
    {
        l_want[i] = ' ';
        l_have[i] = ' ';
    }
    l_cursor = NOPOS;
    if (!ok)
    {
        l_addr = 0u;
    }
    return ok;
}

uint8_t lcd_address(void) { return l_addr; }

void lcd_put(uint8_t row, uint8_t col, char c)
{
    if ((row < LCD_ROWS) && (col < LCD_COLS))
    {
        l_want[(row * LCD_COLS) + col] = c;
    }
}

void lcd_puts(uint8_t row, uint8_t col, const char *s)
{
    while ((*s != '\0') && (col < LCD_COLS))
    {
        lcd_put(row, col++, *s++);
    }
}

void lcd_fill(uint8_t row, char c)
{
    uint8_t col;
    for (col = 0u; col < LCD_COLS; col++)
    {
        lcd_put(row, col, c);
    }
}

void lcd_backlight(bool on)
{
    uint8_t bl = on ? B_BL : 0u;
    if (bl != l_bl)
    {
        l_bl = bl;
        l_bl_dirty = true;
    }
}

bool lcd_pending(void)
{
    uint8_t i;
    if (l_bl_dirty)
    {
        return true;
    }
    for (i = 0u; i < CELLS; i++)
    {
        if (l_want[i] != l_have[i])
        {
            return true;
        }
    }
    return false;
}

lcd_step_t lcd_step(uint8_t max_cells)
{
    uint8_t done = 0u, seen;

    if (l_addr == 0u)
    {
        return LCD_FAIL;
    }
    if (l_bl_dirty)
    {
        uint8_t b = l_bl;                     /* EN low: the controller ignores it */
        if (!twi_write(l_addr, &b, 1u)) { return LCD_FAIL; }
        l_bl_dirty = false;
    }
    for (seen = 0u; (seen < CELLS) && (done < max_cells); seen++)
    {
        uint8_t i = l_scan;
        l_scan = (uint8_t)((l_scan + 1u) % CELLS);
        if (l_want[i] == l_have[i])
        {
            continue;
        }
        if (l_cursor != i)
        {
            uint8_t ddram = (uint8_t)(((i / LCD_COLS) != 0u ? 0x40u : 0x00u) + (i % LCD_COLS));
            if (!l_cmd((uint8_t)(0x80u | ddram))) { return LCD_FAIL; }
        }
        if (!l_data((uint8_t)l_want[i])) { return LCD_FAIL; }
        l_have[i] = l_want[i];
        /* auto-increment; past column 15 DDRAM leaves the visible line */
        l_cursor = ((i % LCD_COLS) == (LCD_COLS - 1u)) ? NOPOS : (uint8_t)(i + 1u);
        done++;
    }
    return lcd_pending() ? LCD_MORE : LCD_IDLE;
}
