/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file adc_pwm.c
 * @brief ADC demo for the ATmega328P co-simulation.
 *
 * - ADC0 (Arduino A0): potentiometer, AVCC reference, 10-bit conversion.
 * - PD6  (Arduino D6): fast PWM (Timer0, OC0A, ~7.8 kHz), duty = ADC / 4.
 * - PB5  (Arduino D13): high when ADC >= 512 (Vin >= Vref / 2).
 *
 * Build: make adc_pwm.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>

static void adcInit(void)
{
    ADMUX  = _BV(REFS0);                                 /* AVCC, channel 0. */
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0); /* clk/128. */
}

static uint16_t adcRead(void)
{
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC))
    {
        /* Wait for the end of conversion. */
    }
    return ADC;    /* Reads ADCL then ADCH. */
}

static void pwmInit(void)
{
    /* Fast PWM, non-inverting on OC0A, clk/8: 16 MHz / 8 / 256 = 7.8 kHz. */
    TCCR0A = _BV(COM0A1) | _BV(WGM01) | _BV(WGM00);
    TCCR0B = _BV(CS01);
    OCR0A  = 0;
}

int main(void)
{
    DDRD |= _BV(DDD6);
    DDRB |= _BV(DDB5);

    adcInit();
    pwmInit();

    for (;;)
    {
        const uint16_t value = adcRead();

        OCR0A = (uint8_t)(value >> 2);

        if (value >= 512u)
        {
            PORTB |= _BV(PORTB5);
        }
        else
        {
            PORTB &= (uint8_t)~_BV(PORTB5);
        }
    }
}
