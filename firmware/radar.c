/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file radar.c
 * @brief HC-SR04 distance sensor and servomotor: the servo follows the distance.
 *
 *   D7 (PD7) -> TRIG of the HC-SR04 (12 us pulse every 60 ms)
 *   D8 (PB0) <- ECHO (high for 58 us per cm)
 *   D9 (PB1, OC1A) -> servo signal, 50 Hz (Timer1, 0.5 us steps)
 * 5 cm -> 0 deg, 55 cm -> 180 deg (pulse 544..2400 us, as the Servo library).
 * Serial 115200 bauds: "Distance = 40.0 cm, servo 126 deg".
 *
 * Build: make radar.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

#define PERIOD_TICKS 40000u                       /* 20 ms at 2 MHz */

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);                           /* 16 MHz / 8 / 17 = ~115200 bauds. */
    UBRR0  = 16;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);           /* 8N1 */
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

static void uartDec(uint16_t value)
{
    char buf[6];
    uint8_t n = 0;
    do
    {
        buf[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    while (n > 0u)
    {
        uartPutc(buf[--n]);
    }
}

/* Servo on OC1A: fast PWM, TOP = ICR1, 50 Hz, 0.5 us per tick. */
static void servoInit(void)
{
    DDRB  |= _BV(DDB1);
    ICR1   = PERIOD_TICKS - 1u;
    OCR1A  = 3000u;                               /* 1.5 ms: middle */
    TCCR1A = _BV(COM1A1) | _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11); /* Mode 14, clk / 8 */
}

static void servoWrite(uint8_t degrees)
{
    const uint16_t us = (uint16_t)(544u + (uint32_t)degrees * (2400u - 544u) / 180u);
    OCR1A = (uint16_t)(us * 2u);
}

/* Echo width in us (0: no echo within 30 ms). Timer1 counts 0.5 us ticks. */
static uint16_t measureUs(void)
{
    PORTD |= _BV(PORTD7);
    _delay_us(12);
    PORTD &= (uint8_t)~_BV(PORTD7);

    uint16_t wait = 0;
    while (!(PINB & _BV(PINB0)))
    {
        _delay_us(2);
        if (++wait > 2000u)
        {
            return 0;                             /* No echo started */
        }
    }
    const uint16_t start = TCNT1;
    wait = 0;
    while (PINB & _BV(PINB0))
    {
        _delay_us(2);
        if (++wait > 10000u)
        {
            return 0;                             /* Out of range (38 ms) */
        }
    }
    const uint16_t end = TCNT1;
    const uint16_t ticks = (uint16_t)((end + PERIOD_TICKS - start) % PERIOD_TICKS);
    return (uint16_t)(ticks / 2u);
}

int main(void)
{
    DDRD |= _BV(DDD7);                            /* TRIG */
    uartInit();
    servoInit();
    uartPuts("Radar : HC-SR04 + servo\r\n");

    for (;;)
    {
        const uint16_t us = measureUs();
        const uint16_t tenths = (uint16_t)((uint32_t)us * 10u / 58u);    /* 0.1 cm */
        uint16_t span = tenths < 50u ? 0u : tenths - 50u;                 /* from 5 cm */
        if (span > 500u)
        {
            span = 500u;
        }
        const uint8_t degrees = (uint8_t)((uint32_t)span * 180u / 500u);
        servoWrite(degrees);

        if (us == 0u)
        {
            uartPuts("Distance : hors de portee\r\n");
        }
        else
        {
            uartPuts("Distance = ");
            uartDec(tenths / 10u);
            uartPutc('.');
            uartDec(tenths % 10u);
            uartPuts(" cm, servo ");
            uartDec(degrees);
            uartPuts(" deg\r\n");
        }
        _delay_ms(55);                            /* One measure every 60 ms */
    }
}
