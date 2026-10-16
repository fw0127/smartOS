/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <avr/io.h>
#include <util/twi.h>
#include "twi.h"

/* One byte at 100 kHz takes ~90 us = ~1440 CPU cycles; a wait loop pass
 * is a few cycles, so 4000 passes is a generous per-step bound. */
#define L_SPIN  4000u

static bool l_wait(void)
{
    uint16_t n = L_SPIN;
    while ((TWCR & _BV(TWINT)) == 0u)
    {
        if (--n == 0u)
        {
            return false;
        }
    }
    return true;
}

static void l_stop(void)
{
    uint16_t n = L_SPIN;
    TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
    while (((TWCR & _BV(TWSTO)) != 0u) && (--n != 0u)) { }
}

void twi_init(void)
{
    PORTC |= _BV(PC4) | _BV(PC5);         /* weak internal pull-ups; backpacks add 4.7k */
    TWSR = 0u;                            /* prescaler 1 */
    TWBR = (uint8_t)(((F_CPU / 100000ul) - 16ul) / 2ul);   /* 72 -> 100 kHz */
    TWCR = _BV(TWEN);
}

static bool l_start(uint8_t addr7)
{
    TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
    if (!l_wait() || ((TW_STATUS != TW_START) && (TW_STATUS != TW_REP_START)))
    {
        return false;
    }
    TWDR = (uint8_t)(addr7 << 1);         /* write */
    TWCR = _BV(TWINT) | _BV(TWEN);
#ifdef TWI_SIMAVR_QUIRK
    /* simulation build only: simavr 1.6 reports an ACKed SLA+W as 0x28 */
    return l_wait() && ((TW_STATUS == TW_MT_SLA_ACK) || (TW_STATUS == TW_MT_DATA_ACK));
#else
    return l_wait() && (TW_STATUS == TW_MT_SLA_ACK);
#endif
}

bool twi_write(uint8_t addr7, const uint8_t *data, uint8_t len)
{
    bool ok = l_start(addr7);
    uint8_t i;

    for (i = 0u; ok && (i < len); i++)
    {
        TWDR = data[i];
        TWCR = _BV(TWINT) | _BV(TWEN);
        ok = l_wait() && (TW_STATUS == TW_MT_DATA_ACK);
    }
    l_stop();
    if (!ok)
    {
        TWCR = 0u;                        /* reset the unit after a NACK or timeout */
        TWCR = _BV(TWEN);
    }
    return ok;
}

bool twi_probe(uint8_t addr7)
{
    return twi_write(addr7, 0, 0u);
}
