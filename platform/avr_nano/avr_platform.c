/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * smartOs platform contract on the Arduino Nano (ATmega328P, 16 MHz).
 *
 *   now            Timer1 @ 250 kHz, 16-bit hardware count + software
 *                  overflow counter = 32-bit tick with natural wrap
 *   critical       save SREG + cli at the outermost level, nest by depth
 *   wait_until     arm OCR1A near the deadline, sleep in IDLE mode; any
 *                  interrupt wakes, so it may return early (allowed)
 *   notify         sticky flag; the interrupt that sets it also wakes
 *   health_signal  wdt_reset()
 *   halt           report over serial, then watchdog reset
 */
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <stdlib.h>
#include "avr_platform.h"

#ifndef F_CPU
#error "F_CPU must be defined (16000000UL on the Nano)"
#endif

#define L_HALF        ((sos_tick_t)0x80000000ul)
#define L_MIN_SLEEP   8u          /* ticks (32 us): shorter waits just return */
#define L_MAX_ARM     0xF000u     /* furthest compare point, below one overflow */

static volatile uint16_t l_ovf;           /* high half of the tick */
static volatile bool     l_notified;
static uint8_t           l_crit_depth;    /* touched only with interrupts off */
static uint8_t           l_crit_sreg;
static bool              l_wdt_on;

/* ---- reset cause / early watchdog off ------------------------------------
 * After a watchdog reset the watchdog stays enabled with the shortest
 * timeout; switch it off before main() so start-up cannot loop. */
static uint8_t l_mcusr __attribute__((section(".noinit")));

void avr_early_init(void) __attribute__((naked, used, section(".init3")));
void avr_early_init(void)
{
    l_mcusr = MCUSR;
    MCUSR = 0u;
    wdt_disable();
}

uint8_t avr_reset_cause(void) { return l_mcusr; }

/* ---- interrupts ------------------------------------------------------------ */

ISR(TIMER1_OVF_vect)
{
    l_ovf++;
}

EMPTY_INTERRUPT(TIMER1_COMPA_vect)        /* only wakes wait_until */

/* ---- serial (polled) ------------------------------------------------------ */

static void l_putc(char c)
{
    while ((UCSR0A & _BV(UDRE0)) == 0u) { }
    UDR0 = (uint8_t)c;
}

void avr_puts(const char *s)
{
    while (*s != '\0')
    {
        if (*s == '\n') { l_putc('\r'); }
        l_putc(*s++);
    }
}

void avr_newline(void) { avr_puts("\n"); }

void avr_put_u32(uint32_t v)
{
    char buf[11];
    ultoa(v, buf, 10);
    avr_puts(buf);
}

void avr_put_hex32(uint32_t v)
{
    static const char hex[] = "0123456789ABCDEF";
    int8_t i;
    avr_puts("0x");
    for (i = 28; i >= 0; i -= 4)
    {
        l_putc(hex[(v >> i) & 0xFu]);
    }
}

/* ---- LED ------------------------------------------------------------------- */

void avr_led(bool on)    { if (on) { PORTB |= _BV(PB5); } else { PORTB &= (uint8_t)~_BV(PB5); } }
void avr_led_toggle(void) { PINB = _BV(PB5); }        /* writing PINx toggles */

/* ---- init ------------------------------------------------------------------ */

void avr_platform_init(bool use_watchdog)
{
    cli();

    DDRB |= _BV(PB5);                         /* LED */

    /* UART0 115200 8N1, double speed: UBRR = F_CPU / (8 * baud) - 1 = 16 (2.1 %) */
    UCSR0A = _BV(U2X0);
    UBRR0  = (uint16_t)((F_CPU / (8ul * 115200ul)) - 1ul);
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);

    /* Timer1: normal mode, clk/64 = 250 kHz, overflow interrupt */
    TCCR1A = 0u;
    TCCR1B = 0u;
    TCNT1  = 0u;
    l_ovf  = 0u;
    TIFR1  = _BV(TOV1) | _BV(OCF1A);
    TIMSK1 = _BV(TOIE1);
    TCCR1B = _BV(CS11) | _BV(CS10);

    l_crit_depth = 0u;
    l_notified = false;
    l_wdt_on = use_watchdog;
    if (use_watchdog)
    {
        wdt_enable(WDTO_2S);
    }

    sei();
}

/* ---- contract -------------------------------------------------------------- */

sos_tick_t sos_platform_now(void)
{
    uint8_t  s = SREG;
    uint16_t hi, lo;

    cli();
    hi = l_ovf;
    lo = TCNT1;
    /* overflow happened but its interrupt has not run yet (we hold it off):
     * a small lo means the count already wrapped */
    if (((TIFR1 & _BV(TOV1)) != 0u) && (lo < 0x8000u))
    {
        hi++;
    }
    SREG = s;
    return ((sos_tick_t)hi << 16) | lo;
}

void sos_platform_critical_enter(void)
{
    uint8_t s = SREG;
    cli();
    if (l_crit_depth == 0u)
    {
        l_crit_sreg = s;
    }
    l_crit_depth++;
}

void sos_platform_critical_exit(void)
{
    /* interrupts are off here: we are inside the section */
    if (l_crit_depth == 0u)
    {
        sos_platform_halt(0xC0DE0001ul);       /* unbalanced exit */
    }
    l_crit_depth--;
    if (l_crit_depth == 0u)
    {
        SREG = l_crit_sreg;                   /* restores the I bit as it was */
    }
}

void sos_platform_wait_until(sos_tick_t deadline)
{
    uint8_t    s = SREG;
    sos_tick_t dist;

    cli();
    if (l_notified)
    {
        l_notified = false;                   /* sticky notify: return at once */
        SREG = s;
        return;
    }
    dist = (sos_tick_t)(deadline - sos_platform_now());
    if ((dist >= L_HALF) || (dist < L_MIN_SLEEP))
    {
        SREG = s;                             /* reached, or too close to sleep */
        return;
    }
    if (dist > L_MAX_ARM)
    {
        dist = L_MAX_ARM;                     /* return early; the caller waits again */
    }
    OCR1A  = (uint16_t)(TCNT1 + (uint16_t)dist);
    TIFR1  = _BV(OCF1A);
    TIMSK1 |= _BV(OCIE1A);

    set_sleep_mode(SLEEP_MODE_IDLE);          /* timers and UART keep running */
    sleep_enable();
    sei();                                    /* the next instruction runs before any */
    sleep_cpu();                              /* pending interrupt: no lost wake-up */
    sleep_disable();

    cli();
    TIMSK1 &= (uint8_t)~_BV(OCIE1A);
    l_notified = false;                       /* a notify during the sleep ended it */
    SREG = s;
}

void sos_platform_notify(void)
{
    l_notified = true;                        /* single byte: atomic on AVR */
}

void sos_platform_health_signal(void)
{
    if (l_wdt_on)
    {
        wdt_reset();
    }
}

void sos_platform_halt(uint32_t reason)
{
    cli();
    avr_led(true);
    avr_newline();
    avr_puts("smartOs HALT ");
    avr_put_hex32(reason);
    avr_puts("\r");
    UCSR0A |= _BV(TXC0);                      /* clear, then send the last byte */
    l_putc('\n');
    while ((UCSR0A & _BV(TXC0)) == 0u) { }    /* let the last byte leave */
    wdt_enable(WDTO_15MS);
    for (;;) { }
}
