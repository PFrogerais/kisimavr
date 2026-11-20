/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart_adc.c
 * @brief UART demo for the ATmega328P co-simulation.
 *
 * Prints a banner, then the ADC0 reading in a loop on UART0 (TXD = PD1),
 * 115200 bauds 8N1 (U2X0 = 1, UBRR0 = 16).
 *
 * Build: make uart_adc.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);                     /* Double speed: 16 MHz / 8 / 17. */
    UBRR0  = 16;                            /* ~115200 bauds (+2.1 %).        */
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);     /* 8 data bits, no parity, 1 stop. */
}

static void uartPutc(char c)
{
    while (!(UCSR0A & _BV(UDRE0)))
    {
        /* Wait for an empty transmit buffer. */
    }
    UDR0 = (uint8_t)c;
}

static void uartPuts(const char *s)
{
    while (*s != '\0')
    {
        uartPutc(*s++);
    }
}

static void uartPutu(uint16_t value)
{
    char    digits[5];
    uint8_t n = 0;

    do
    {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    while (n > 0u)
    {
        uartPutc(digits[--n]);
    }
}

static void adcInit(void)
{
    ADMUX  = _BV(REFS0);                                        /* AVCC, ADC0. */
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);  /* clk/128.    */
}

static uint16_t adcRead(void)
{
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC))
    {
        /* Wait for the end of conversion. */
    }
    return ADC;
}

int main(void)
{
    uartInit();
    adcInit();

    uartPuts("Hello STI2D !\r\n");

    for (;;)
    {
        const uint16_t raw = adcRead();
        const uint16_t mv  = (uint16_t)(((uint32_t)raw * 5000u) / 1024u);

        uartPuts("ADC0 = ");
        uartPutu(raw);
        uartPuts(" (");
        uartPutu(mv);
        uartPuts(" mV)\r\n");
    }
}
