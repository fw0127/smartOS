/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/* Polled I2C (TWI) master for the ATmega328P: SDA = A4, SCL = A5.
 * Every wait is bounded, so a missing or stuck device returns false
 * instead of hanging the scheduler. */
#ifndef TWI_H
#define TWI_H

#include <stdbool.h>
#include <stdint.h>

void twi_init(void);                                    /* 100 kHz */
bool twi_write(uint8_t addr7, const uint8_t *data, uint8_t len);
bool twi_probe(uint8_t addr7);                          /* address ACKed? */

#endif
