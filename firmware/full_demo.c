/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file full_demo.c
 * @brief Demo firmware for the complete ATMEGA328P model (Arduino Uno pins).
 *
 * - D2  (PD2): push-button to GND, internal pull-up (no external resistor).
 * - D13 (PB5): LED, on while the button is pressed.
 * - A0  (PC0): potentiometer, read by the ADC.
 * - D6  (PD6): PWM (Timer0, OC0A), duty = A0 / 4.
 * - D1  (PD1): UART TX, 115200 bauds: "A0 = 512  BTN = 1".
 *
 * Build: make full_demo.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);                    /* 16 MHz / 8 / 17 = ~115200 bauds. */
    UBRR0  = 16;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);    /* 8N1 */
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

static uint16_t adcRead(uint8_t channel)
{
    ADMUX   = _BV(REFS0) | (channel & 0x07u);    /* AVCC reference. */
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC))
    {
        /* Wait for the end of conversion. */
    }
    return ADC;
}

int main(void)
{
    /* Button on PD2: input with internal pull-up. */
    DDRD  &= (uint8_t)~_BV(DDD2);
    PORTD |= _BV(PORTD2);

    /* LED on PB5, PWM on PD6. */
    DDRB |= _BV(DDB5);
    DDRD |= _BV(DDD6);

    /* Timer0 fast PWM on OC0A, clk/8 -> 7.8 kHz. */
    TCCR0A = _BV(COM0A1) | _BV(WGM01) | _BV(WGM00);
    TCCR0B = _BV(CS01);

    /* ADC on, clk/128. */
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);

    uartInit();
    uartPuts("ATmega328P full model\r\n");

    for (;;)
    {
        const uint8_t  pressed = (PIND & _BV(PIND2)) == 0u;    /* Active low. */
        const uint16_t a0      = adcRead(0);

        if (pressed)
        {
            PORTB |= _BV(PORTB5);
        }
        else
        {
            PORTB &= (uint8_t)~_BV(PORTB5);
        }

        OCR0A = (uint8_t)(a0 >> 2);

        uartPuts("A0 = ");
        uartPutu(a0);
        uartPuts("  BTN = ");
        uartPutc(pressed ? '1' : '0');
        uartPuts("\r\n");
    }
}
