/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* HD44780 16x2 character LCD behind a PCF8574 I2C backpack.
 *
 * Writers never touch the bus: they update a shadow copy of the screen.
 * lcd_step() pushes at most 'max_cells' changed characters per call, so a
 * cooperative task can refresh the display in short steps (~0.5 ms per
 * character at 100 kHz) and yield in between. */
#ifndef LCD1602_H
#define LCD1602_H

#include <stdbool.h>
#include <stdint.h>

#define LCD_COLS  16u
#define LCD_ROWS  2u

typedef enum { LCD_IDLE, LCD_MORE, LCD_FAIL } lcd_step_t;

/* Probe 0x27 (PCF8574) and 0x3F (PCF8574A), then run the HD44780 4-bit
 * initialisation (blocks ~60 ms). Returns false if no backpack answers. */
bool    lcd_init(void);
uint8_t lcd_address(void);                     /* 0 when absent */

void lcd_put(uint8_t row, uint8_t col, char c);  /* shadow only */
void lcd_puts(uint8_t row, uint8_t col, const char *s);
void lcd_fill(uint8_t row, char c);
void lcd_backlight(bool on);                   /* applied by the next lcd_step */

bool       lcd_pending(void);
lcd_step_t lcd_step(uint8_t max_cells);

#endif
