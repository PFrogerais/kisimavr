/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file blink.c
 * @brief Demo firmware for the ATmega328P co-simulation.
 *
 * - PB5 (Arduino D13): 1 kHz square wave from a Timer0 CTC interrupt.
 * - PB0 (Arduino D8) : copy of the push-button input PD2 (Arduino D2).
 *
 * Build: make  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <avr/interrupt.h>

/* Timer0 compare match: toggles PB5 every 500 us. */
ISR(TIMER0_COMPA_vect)
{
    PINB = _BV(PINB5);    /* Writing 1 to PINx toggles PORTx. */
}

static void timer0Init(void)
{
    /* CTC mode, prescaler 64: 16 MHz / 64 = 250 kHz -> 125 counts = 500 us. */
    TCCR0A = _BV(WGM01);
    TCCR0B = _BV(CS01) | _BV(CS00);
    OCR0A  = 124;
    TIMSK0 = _BV(OCIE0A);
}

int main(void)
{
    DDRB  = _BV(DDB5) | _BV(DDB0);    /* PB5, PB0 outputs. */
    DDRD &= (uint8_t)~_BV(DDD2);      /* PD2 input, no pull-up. */

    timer0Init();
    sei();

    for (;;)
    {
        if (PIND & _BV(PIND2))
        {
            PORTB |= _BV(PORTB0);
        }
        else
        {
            PORTB &= (uint8_t)~_BV(PORTB0);
        }
    }
}
