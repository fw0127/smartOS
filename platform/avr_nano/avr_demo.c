/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * smartOs on the Arduino Nano: the kernel on real silicon.
 *
 *   button  (prio 0) event: a falling edge on D2 (INT0) sets EV_PRESS from
 *                    the interrupt via sos_event_set_async; 50 ms debounce
 *   blink   (prio 1) periodic 250 ms: toggles the LED on D13
 *   report  (prio 2) periodic 1 s: uptime, idle wake-ups, presses, errors
 *
 * The watchdog (2 s) is served by the kernel's health signal, so a hung
 * scheduler resets the board. Wire a push button between D2 and GND; the
 * internal pull-up is on. A KY-004 module needs all three pins (S to D2,
 * middle to 5V, - to GND): its on-board resistor may be a pull-down.
 * Serial: 115200 8N1.
 */
#include <avr/interrupt.h>
#include <avr/io.h>
#include "sos_pub.h"
#include "avr_platform.h"

enum { T_BUTTON, T_BLINK, T_REPORT };
#define EV_PRESS  0x1u

static uint32_t   g_presses;
static uint32_t   g_idle;
static sos_tick_t g_last_press;
static bool       g_pressed_once;

ISR(INT0_vect)
{
    (void)sos_event_set_async(T_BUTTON, EV_PRESS);
}

static sos_run_result_t button_run(void *ctx, sos_events_t ev)
{
    sos_tick_t now = sos_time_now();
    (void)ctx;

    if ((ev & EV_PRESS) == 0u)
    {
        return SOS_RUN_OK;
    }
    if (g_pressed_once && !sos_time_elapsed(g_last_press, sos_time_to_ticks(50000u)))
    {
        return SOS_RUN_OK;                       /* bounce */
    }
    g_pressed_once = true;
    g_last_press = now;
    g_presses++;
    avr_puts("button press #");
    avr_put_u32(g_presses);
    avr_newline();
    return SOS_RUN_OK;
}

static sos_run_result_t blink_run(void *ctx, sos_events_t ev)
{
    (void)ctx;
    (void)ev;
    avr_led_toggle();
    return SOS_RUN_OK;
}

static sos_run_result_t report_run(void *ctx, sos_events_t ev)
{
    (void)ctx;
    (void)ev;
    avr_puts("t=");
    avr_put_u32(sos_ticks_to_time(sos_time_now()) / 1000u);
    avr_puts(" ms  idle_wakeups=");
    avr_put_u32(g_idle);
    avr_puts("  presses=");
    avr_put_u32(g_presses);
    avr_puts("  errors=");
    avr_put_u32(sos_error_count());
    avr_newline();
    return SOS_RUN_OK;
}

static void on_idle(void)
{
    g_idle++;
}

static void on_fatal(sos_status_t code)
{
    avr_puts("fatal ");
    avr_put_u32((uint32_t)code);
    avr_newline();
}

static const sos_task_desc_t g_tasks[] =
{
    /* name      prio trigger            onerr               maxr again period   offset budget alive mask      init  run         done  ctx */
    { "button",  0u, SOS_TRIG_EVENT,    SOS_ON_ERR_IGNORE,  0u,  0u,  0u,      0u,    0u,    0u,   EV_PRESS, NULL, button_run, NULL, NULL },
    { "blink",   1u, SOS_TRIG_PERIODIC, SOS_ON_ERR_FATAL,   0u,  0u,  250000u, 0u,    0u,    0u,   0u,       NULL, blink_run,  NULL, NULL },
    { "report",  2u, SOS_TRIG_PERIODIC, SOS_ON_ERR_FATAL,   0u,  0u,  1000000u,0u,    0u,    0u,   0u,       NULL, report_run, NULL, NULL },
};

static const sos_hooks_t g_hooks =
{
    NULL,           /* on_overrun */
    NULL,           /* on_task_error */
    on_fatal,
    on_idle,
    NULL            /* on_ring_overflow */
};

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
    avr_puts(" on Arduino Nano, reset cause MCUSR=");
    avr_put_hex32(avr_reset_cause());
    avr_newline();

    /* D2 input with pull-up, INT0 on the falling edge */
    DDRD  &= (uint8_t)~_BV(PD2);
    PORTD |= _BV(PD2);
    EICRA  = _BV(ISC01);
    EIFR   = _BV(INTF0);

    if (sos_init(&g_root) != SOS_OK)
    {
        avr_puts("sos_init failed");
        avr_newline();
        sos_platform_halt(0xBAD0C0F6ul);
    }
    EIMSK = _BV(INT0);                           /* only once the kernel is ready */
    sos_start();                                 /* does not return */
    return 0;
}
