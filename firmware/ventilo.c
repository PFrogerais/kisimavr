/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file ventilo.c
 * @brief Fan regulation: NTC thermistor on A0, DC motor through an L298N.
 *
 *   A0 (PC0) <- divider 10 kohm (to 5 V) / NTC 10 kohm B = 3950 (to GND)
 *   D6 (PD6, OC0A) -> ENA of the L298N, PWM 976 Hz (Timer0)
 *   D7 (PD7) -> IN1 = 1, D8 (PB0) -> IN2 = 0: one direction
 * Below 25 degC the motor stops; from 25 to 35 degC the duty cycle goes
 * from 30 to 100 %. Serial 115200 bauds every 20 ms: "T = 27.4 C, moteur 51 %".
 *
 * Build: make ventilo.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

/* NTC 10 kohm B = 3950: resistance (ohm) from -10 to 80 degC, 5 degC steps. */
static const uint16_t kNtc[] = { 58246u, 44026u, 33621u, 25925u, 20175u, 15837u, 12535u,
                                 10000u, 8037u, 6506u, 5301u, 4348u, 3588u, 2978u, 2486u,
                                 2086u, 1760u, 1492u, 1270u };
#define NTC_COUNT (sizeof kNtc / sizeof kNtc[0])

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

static uint16_t adcRead(void)
{
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC))
    {
        /* Conversion. */
    }
    return ADC;
}

/* Temperature in tenths of degC + 100 (unsigned) from the NTC resistance. */
static uint16_t ntcTenths(uint32_t ohm)
{
    if (ohm >= kNtc[0])
    {
        return 0u;                                /* -10 degC or colder */
    }
    for (uint8_t i = 0; i + 1u < NTC_COUNT; ++i)
    {
        if (ohm > kNtc[i + 1u])
        {
            const uint32_t step = kNtc[i] - kNtc[i + 1u];
            return (uint16_t)(50u * i + 50u * (kNtc[i] - ohm) / step);
        }
    }
    return (uint16_t)(50u * (NTC_COUNT - 1u));    /* 80 degC or hotter */
}

/* Duty cycle of ENA (OC0A); 0 % leaves a 62 ns spike per period, negligible. */
static void motorDuty(uint8_t percent)
{
    OCR0A = (uint8_t)((uint16_t)percent * 255u / 100u);
}

int main(void)
{
    DDRD |= _BV(DDD6) | _BV(DDD7);                /* ENA, IN1 */
    DDRB |= _BV(DDB0);                            /* IN2 */
    PORTD |= _BV(PORTD7);                         /* IN1 = 1, IN2 = 0 */
    TCCR0A = _BV(COM0A1) | _BV(WGM01) | _BV(WGM00);   /* Fast PWM on OC0A */
    OCR0A  = 0;
    TCCR0B = _BV(CS01) | _BV(CS00);               /* clk / 64: 976 Hz */
    ADMUX  = _BV(REFS0);                          /* AVcc, A0 */
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
    uartInit();
    uartPuts("Ventilateur : CTN + L298N\r\n");

    for (;;)
    {
        const uint16_t adc = adcRead();
        const uint32_t ohm = adc >= 1023u ? 100000u : 10000ul * adc / (1023u - adc);
        const uint16_t t = ntcTenths(ohm);        /* tenths + 100 */
        uint8_t duty = 0;
        if (t >= 450u)
        {
            duty = 100;                           /* 35 degC and more */
        }
        else if (t >= 350u)
        {
            duty = (uint8_t)(30u + (uint32_t)(t - 350u) * 70u / 100u);
        }
        motorDuty(duty);

        uartPuts("T = ");
        if (t < 100u)
        {
            uartPutc('-');
            uartDec((uint16_t)((100u - t) / 10u));
            uartPutc('.');
            uartDec((uint16_t)((100u - t) % 10u));
        }
        else
        {
            uartDec((uint16_t)((t - 100u) / 10u));
            uartPutc('.');
            uartDec((uint16_t)((t - 100u) % 10u));
        }
        uartPuts(" C, moteur ");
        uartDec(duty);
        uartPuts(" %\r\n");
        _delay_ms(18);
    }
}
