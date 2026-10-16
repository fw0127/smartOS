/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * smartOs on the Arduino Nano: 4x4 keypad + LCD1602 (I2C).
 *
 *   keypad (prio 0) periodic 10 ms: scan + debounce, edit the text on line 2
 *   lcd    (prio 1) event EV_DIRTY: push at most 4 changed characters per
 *                   step (~0.5 ms each over I2C), return AGAIN while more
 *                   remain -- the scan is never held up by a slow refresh
 *   clock  (prio 2) periodic 1 s: uptime and key count on line 1, serial report
 *
 * Keys: digits and B C D type, '*' deletes, '#' clears, 'A' toggles the
 * backlight. All tasks share the screen's shadow copy without locking:
 * steps run to completion, so none can see another half-way.
 *
 * Wiring: keypad ribbon pins 1..8 -> D4..D11; LCD backpack SDA -> A4,
 * SCL -> A5, VCC -> 5V, GND -> GND. Serial 115200 8N1.
 */
#include <avr/io.h>
#include "sos_pub.h"
#include "avr_platform.h"
#include "keypad.h"
#include "lcd1602.h"

enum { T_KEYPAD, T_LCD, T_CLOCK };
#define EV_DIRTY  0x1u

static char     g_text[LCD_COLS + 1u];
static uint8_t  g_len;
static uint16_t g_keys;
static bool     g_backlight = true;
static bool     g_lcd_ok;

/* ---- helpers -------------------------------------------------------------- */

static void put_dec(char *dst, uint32_t v, uint8_t width, char pad)
{
    while (width > 0u)
    {
        width--;
        dst[width] = (char)('0' + (v % 10u));
        v /= 10u;
        if (v == 0u)
        {
            while (width > 0u) { dst[--width] = pad; }
        }
    }
}

static void show_text(void)
{
    uint8_t i;
    for (i = 0u; i < LCD_COLS; i++)
    {
        lcd_put(1u, i, (i < g_len) ? g_text[i] : ' ');
    }
    if (g_lcd_ok)
    {
        (void)sos_event_set(T_LCD, EV_DIRTY);
    }
}

/* ---- tasks ----------------------------------------------------------------- */

static sos_run_result_t keypad_run(void *ctx, sos_events_t ev)
{
    char k = keypad_scan();
    (void)ctx;
    (void)ev;

    if (k == KEY_NONE)
    {
        return SOS_RUN_OK;
    }
    g_keys++;
    if (k == '*')
    {
        if (g_len > 0u) { g_len--; }
    }
    else if (k == '#')
    {
        g_len = 0u;
    }
    else if (k == 'A')
    {
        g_backlight = !g_backlight;
        lcd_backlight(g_backlight);
    }
    else
    {
        if (g_len == LCD_COLS)            /* full: scroll left by one */
        {
            uint8_t i;
            for (i = 1u; i < LCD_COLS; i++) { g_text[i - 1u] = g_text[i]; }
            g_len--;
        }
        g_text[g_len++] = k;
    }
    g_text[g_len] = '\0';
    show_text();

    {
        /* short on purpose: polled 115200 baud costs ~87 us per character,
         * and this is the highest-priority task with a 1 ms budget */
        char s[8] = { 'k', 'e', 'y', ' ', k, '\r', '\n', '\0' };
        avr_puts(s);
    }
    return SOS_RUN_OK;
}

static sos_run_result_t lcd_run(void *ctx, sos_events_t ev)
{
    (void)ctx;
    (void)ev;
    switch (lcd_step(4u))
    {
    case LCD_MORE: return SOS_RUN_AGAIN;
    case LCD_IDLE: return SOS_RUN_OK;
    default:       return SOS_RUN_ERR;    /* bus error: logged, policy IGNORE */
    }
}

static sos_run_result_t clock_run(void *ctx, sos_events_t ev)
{
    /* "up HH:MM:SS k NNN" */
    char line[LCD_COLS + 1u] = "up 00:00:00 k   ";
    uint32_t s = sos_ticks_to_time(sos_time_now()) / 1000000ul;
    sos_task_stats_t st;
    (void)ctx;
    (void)ev;

    put_dec(&line[3], (s / 3600u) % 100u, 2u, '0');
    put_dec(&line[6], (s / 60u) % 60u, 2u, '0');
    put_dec(&line[9], s % 60u, 2u, '0');
    put_dec(&line[13], g_keys % 1000u, 3u, ' ');
    lcd_puts(0u, 0u, line);
    if (g_lcd_ok)
    {
        (void)sos_event_set(T_LCD, EV_DIRTY);
    }

    avr_puts(line);
    (void)sos_task_get_stats(T_KEYPAD, &st);
    avr_puts("  [");
    avr_puts(g_text);
    avr_puts("]  keypad max step ");
    avr_put_u32(sos_ticks_to_time(st.max_step));
    avr_puts(" us, overruns ");
    avr_put_u32(st.overrun_cnt);
    (void)sos_task_get_stats(T_LCD, &st);
    avr_puts(", lcd max step ");
    avr_put_u32(sos_ticks_to_time(st.max_step));
    avr_puts(" us, steps/job ");
    avr_put_u32(st.max_job_steps);
    avr_puts(", errors ");
    avr_put_u32(sos_error_count());
    avr_newline();
    return SOS_RUN_OK;
}

static void on_fatal(sos_status_t code)
{
    avr_puts("fatal ");
    avr_put_u32((uint32_t)code);
    avr_newline();
}

/* budgets in us: a keypad step is a few tens of us; four LCD characters
 * with cursor moves are ~4 ms at 100 kHz */
static const sos_task_desc_t g_tasks[] =
{
    /* name     prio trigger            onerr              maxr again period    offset budget alive mask      init  run         done  ctx */
    { "keypad", 0u, SOS_TRIG_PERIODIC, SOS_ON_ERR_FATAL,  0u,  0u,   10000u,   0u,    1000u, 0u,   0u,       NULL, keypad_run, NULL, NULL },
    { "lcd",    1u, SOS_TRIG_EVENT,    SOS_ON_ERR_IGNORE, 0u,  4u,   0u,       0u,    8000u, 0u,   EV_DIRTY, NULL, lcd_run,    NULL, NULL },
    { "clock",  2u, SOS_TRIG_PERIODIC, SOS_ON_ERR_FATAL,  0u,  0u,   1000000u, 0u,    0u,    0u,   0u,       NULL, clock_run,  NULL, NULL },
};

static const sos_hooks_t g_hooks = { NULL, NULL, on_fatal, NULL, NULL };

static const sos_cfg_root_t g_root =
{
    SOS_ABI_VERSION, 0u /* hand-written */, 0u, g_tasks,
    (uint8_t)(sizeof g_tasks / sizeof g_tasks[0]),
    NULL, 0u,
    &g_hooks
};

int main(void)
{
    avr_platform_init(true);
    avr_newline();
    avr_puts("smartOs ");
    avr_put_hex32(sos_version());
    avr_puts(" keypad + LCD1602 demo");
    avr_newline();

    keypad_init();
    g_lcd_ok = lcd_init();                    /* ~60 ms, before the scheduler starts */
    if (g_lcd_ok)
    {
        avr_puts("LCD at I2C address ");
        avr_put_hex32(lcd_address());
    }
    else
    {
        avr_puts("no LCD on I2C 0x27/0x3F -- serial output only");
    }
    avr_newline();
    lcd_puts(0u, 0u, "smartOs");
    lcd_puts(1u, 0u, "press a key...");

    if (sos_init(&g_root) != SOS_OK)
    {
        avr_puts("sos_init failed");
        avr_newline();
        sos_platform_halt(0xBAD0C0F6ul);
    }
    if (g_lcd_ok)
    {
        (void)sos_event_set_async(T_LCD, EV_DIRTY);
    }
    sos_start();                              /* does not return */
    return 0;
}
