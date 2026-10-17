/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Contract-suite harness for the Arduino Nano. Real time from Timer1.
 *
 * Asynchronous context: none for the generic suite. Its async checks run a
 * long loop concurrently with the main flow (thread style); a single-core
 * MCU can only offer interrupts, which run to completion and cannot loop
 * until the main flow tells them to stop. Those two checks are reported as
 * skipped, and the property that matters on this target -- an interrupt
 * ends an ongoing wait through notify -- is checked by the extra
 * target-specific checks below, with Timer2 as a one-shot interrupt.
 *
 * Output: serial, 115200 8N1. Ends with "CONTRACT PASS" or "CONTRACT FAIL n"
 * and blinks the LED slowly on pass, fast on failure.
 */
#include <avr/interrupt.h>
#include <avr/io.h>
#include "avr_platform.h"
#include "sos_contract_test.h"

/* ---- harness for the generic suite ------------------------------------- */

void sos_ct_setup(void) { }

void sos_ct_let_time_pass(sos_tick_t ticks)
{
    sos_tick_t t0 = sos_platform_now();
    while ((sos_tick_t)(sos_platform_now() - t0) < ticks) { }
}

bool sos_ct_async_available(void) { return false; }
void sos_ct_async_start(void (*fn)(void *arg), void *arg) { (void)fn; (void)arg; }
void sos_ct_async_join(void) { }

void sos_ct_print(const char *line)
{
    avr_puts("  ");
    avr_puts(line);
    avr_newline();
}

/* ---- target-specific checks ---------------------------------------------- */

static volatile uint8_t  l_t2_count;      /* Timer2 compare interrupts left */
static volatile uint8_t  l_t2_fired;
static volatile bool     l_t2_notify;

/* Timer2 CTC, clk/1024, OCR2A = 155 -> one interrupt every ~10 ms */
static void l_t2_start(uint8_t periods, bool notify)
{
    uint8_t s = SREG;
    cli();
    l_t2_count = periods;
    l_t2_fired = 0u;
    l_t2_notify = notify;
    TCCR2A = _BV(WGM21);
    TCCR2B = 0u;
    TCNT2  = 0u;
    OCR2A  = 155u;
    TIFR2  = _BV(OCF2A);
    TIMSK2 = _BV(OCIE2A);
    TCCR2B = _BV(CS22) | _BV(CS21) | _BV(CS20);
    SREG = s;
}

static void l_t2_stop(void)
{
    TCCR2B = 0u;
    TIMSK2 = 0u;
}

ISR(TIMER2_COMPA_vect)
{
    (void)sos_platform_now();              /* now is callable from an interrupt */
    if (l_t2_count > 0u)
    {
        l_t2_count--;
        if (l_t2_count == 0u)
        {
            l_t2_fired = 1u;
            if (l_t2_notify)
            {
                sos_platform_notify();
            }
            l_t2_stop();
        }
    }
}

static int l_fail;

static void l_result(const char *name, bool ok)
{
    avr_puts("  ");
    avr_puts(name);
    avr_puts(ok ? "  ok" : "  FAIL");
    avr_newline();
    if (!ok) { l_fail++; }
}

/* An interrupt's notify ends an ongoing wait (~50 ms instead of 1 s). */
static void ct_avr_isr_notify_ends_wait(void)
{
    sos_tick_t t0 = sos_platform_now();
    sos_tick_t deadline = (sos_tick_t)(t0 + SOS_PLATFORM_TICKS_PER_SEC);
    sos_tick_t dt;

    l_t2_start(5u, true);
    while ((l_t2_fired == 0u)
           && ((sos_tick_t)(sos_platform_now() - t0) < SOS_PLATFORM_TICKS_PER_SEC))
    {
        sos_platform_wait_until(deadline);    /* other interrupts may wake it early */
    }
    dt = (sos_tick_t)(sos_platform_now() - t0);
    l_t2_stop();
    l_result("avr: isr notify ends wait", (l_t2_fired != 0u) && (dt < SOS_PLATFORM_TICKS_PER_SEC / 4u));
}

/* A critical section holds interrupts off, also when nested, and releases. */
static void ct_avr_critical_blocks_isr(void)
{
    sos_tick_t t0;
    uint8_t during, after;

    l_t2_start(1u, false);
    sos_platform_critical_enter();
    sos_platform_critical_enter();
    t0 = sos_platform_now();
    while ((sos_tick_t)(sos_platform_now() - t0) < SOS_PLATFORM_TICKS_PER_SEC / 20u) { }  /* 50 ms */
    sos_platform_critical_exit();
    during = l_t2_fired;                      /* still inside the outer section */
    sos_platform_critical_exit();
    t0 = sos_platform_now();
    while ((l_t2_fired == 0u)
           && ((sos_tick_t)(sos_platform_now() - t0) < SOS_PLATFORM_TICKS_PER_SEC / 10u)) { }
    after = l_t2_fired;
    l_t2_stop();
    l_result("avr: critical blocks isr", (during == 0u) && (after != 0u));
}

/* Time stays correct across a Timer1 overflow taken with interrupts off
 * (the pending-overflow correction in now): start just before the hardware
 * count wraps, then stay busy for 10 ms with interrupts off. */
static void ct_avr_now_across_overflow(void)
{
    sos_tick_t t0, t1, prev;
    bool mono = true;

    while (TCNT1 < 0xFF00u) { }               /* <= 1 ms before the wrap */
    sos_platform_critical_enter();
    t0 = sos_platform_now();
    prev = t0;
    do
    {
        t1 = sos_platform_now();
        if ((sos_tick_t)(t1 - prev) >= 0x80000000ul) { mono = false; }
        prev = t1;
    } while ((sos_tick_t)(t1 - t0) < SOS_PLATFORM_TICKS_PER_SEC / 100u);
    sos_platform_critical_exit();
    /* and after the deferred overflow interrupt has run */
    t1 = sos_platform_now();
    l_result("avr: now across overflow", mono && ((sos_tick_t)(t1 - prev) < 2500u));
}

/* wait_until across the 16-bit counter wrap: start each wait just before
 * TCNT1 wraps, so OCR1A = TCNT1 + dist wraps to a small value. The compare
 * must still fire dist ticks later (not one counter period = 262 ms later).
 * 16 trials at different offsets and distances; the wake-up may never be
 * later than 1 ms after the deadline. */
static void ct_avr_wait_across_wrap(void)
{
    uint8_t i;
    sos_tick_t worst = 0u;

    for (i = 0u; i < 16u; i++)
    {
        uint16_t lead = (uint16_t)(16u + 24u * i);           /* ticks before the wrap */
        sos_tick_t dist = (sos_tick_t)(lead + 100u + 50u * i);/* deadline after the wrap */
        sos_tick_t deadline, late;

        while (TCNT1 < (uint16_t)(0xFFFFu - lead)) { }
        deadline = (sos_tick_t)(sos_platform_now() + dist);
        while ((sos_tick_t)(sos_platform_now() - deadline) >= 0x80000000ul)
        {
            sos_platform_wait_until(deadline);                /* may return early */
        }
        late = (sos_tick_t)(sos_platform_now() - deadline);
        if (late > worst) { worst = late; }
    }
    l_result("avr: wait across counter wrap", worst < (SOS_PLATFORM_TICKS_PER_SEC / 1000u));
}

/* Timer1 overflows every 65536 ticks; let several pass with the overflow
 * interrupt working normally and compare against the busy-wait count. */
static void ct_avr_long_run(void)
{
    sos_tick_t t0 = sos_platform_now();
    sos_tick_t prev = t0, t1;
    bool mono = true;

    do
    {
        t1 = sos_platform_now();
        if ((sos_tick_t)(t1 - prev) >= 0x80000000ul) { mono = false; }
        prev = t1;
    } while ((sos_tick_t)(t1 - t0) < 3ul * SOS_PLATFORM_TICKS_PER_SEC);   /* ~11 overflows */
    l_result("avr: 3 s monotonic", mono);
}

int main(void)
{
    int failed;

    avr_platform_init(false);                 /* no watchdog: checks busy-wait */
    avr_newline();
    avr_puts("contract suite: Arduino Nano (ATmega328P), 32-bit ticks @ 250 kHz");
    avr_newline();
    avr_puts("reset cause MCUSR=");
    avr_put_hex32(avr_reset_cause());
    avr_newline();

    failed = sos_contract_run();

    l_fail = 0;
    ct_avr_isr_notify_ends_wait();
    ct_avr_critical_blocks_isr();
    ct_avr_now_across_overflow();
    ct_avr_wait_across_wrap();
    ct_avr_long_run();
    failed += l_fail;

    if (failed == 0)
    {
        avr_puts("CONTRACT PASS");
    }
    else
    {
        avr_puts("CONTRACT FAIL ");
        avr_put_u32((uint32_t)failed);
    }
    avr_newline();

    for (;;)
    {
        sos_tick_t t0 = sos_platform_now();
        sos_tick_t half = (failed == 0) ? SOS_PLATFORM_TICKS_PER_SEC / 2u
                                        : SOS_PLATFORM_TICKS_PER_SEC / 10u;
        avr_led_toggle();
        while ((sos_tick_t)(sos_platform_now() - t0) < half) { }
    }
}
