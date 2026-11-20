/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file lcd_demo.c
 * @brief Character LCD 16x2 (HD44780, 4-bit interface) + voltage on A0.
 *
 * Wiring of the Arduino LiquidCrystal examples, LiquidCrystal(12, 11, 5, 4, 3, 2):
 *   RS = D12 (PB4)   E = D11 (PB3)
 *   D4 = D5 (PD5)    D5 = D4 (PD4)    D6 = D3 (PD3)    D7 = D2 (PD2)
 *   R/W to GND, D0..D3 of the display left open.
 *
 * Row 1: "Bonjour STI2D", row 2: "A0 = 2.50 V", refreshed every 2 ms.
 *
 * Build: make lcd_demo.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

/* ---- Pins --------------------------------------------------------------- */

#define RS_BIT  PB4
#define E_BIT   PB3

static void lcdBus(uint8_t nibble)
{
    /* D4..D7 of the display on PD5, PD4, PD3, PD2 (reversed order). */
    uint8_t d = PORTD & (uint8_t)~(_BV(PD5) | _BV(PD4) | _BV(PD3) | _BV(PD2));
    if (nibble & 0x01u) { d |= _BV(PD5); }
    if (nibble & 0x02u) { d |= _BV(PD4); }
    if (nibble & 0x04u) { d |= _BV(PD3); }
    if (nibble & 0x08u) { d |= _BV(PD2); }
    PORTD = d;
}

/* ---- HD44780 4-bit interface -------------------------------------------- */

static void lcdPulse(void)
{
    PORTB |= _BV(E_BIT);
    _delay_us(1);                         /* E high >= 230 ns.               */
    PORTB &= (uint8_t)~_BV(E_BIT);        /* Falling edge: data latched.     */
}

static void lcdNibble(uint8_t nibble)
{
    lcdBus(nibble);
    _delay_us(1);                         /* Data set-up >= 80 ns.           */
    lcdPulse();
}

static void lcdWrite(uint8_t value, uint8_t isData)
{
    if (isData)
    {
        PORTB |= _BV(RS_BIT);
    }
    else
    {
        PORTB &= (uint8_t)~_BV(RS_BIT);
    }
    lcdNibble(value >> 4);
    lcdNibble(value & 0x0Fu);
    _delay_us(50);                        /* Execution time: 37 us.          */
}

static void lcdCommand(uint8_t cmd)
{
    lcdWrite(cmd, 0);
    if (cmd <= 0x03u)
    {
        _delay_ms(2);                     /* Clear / home: 1.52 ms.          */
    }
}

static void lcdInit(void)
{
    DDRB |= _BV(RS_BIT) | _BV(E_BIT);
    DDRD |= _BV(PD5) | _BV(PD4) | _BV(PD3) | _BV(PD2);

    _delay_ms(15);                        /* Power-up: 15 ms at VCC = 5 V.   */
    PORTB &= (uint8_t)~_BV(RS_BIT);
    lcdNibble(0x3);                       /* Reset sequence (datasheet).     */
    _delay_us(4500);
    lcdNibble(0x3);
    _delay_us(150);
    lcdNibble(0x3);
    _delay_us(50);
    lcdNibble(0x2);                       /* 4-bit interface.                */
    _delay_us(50);

    lcdCommand(0x28);                     /* 4-bit, 2 lines, 5x8 dots.       */
    lcdCommand(0x0C);                     /* Display on, no cursor.          */
    lcdCommand(0x01);                     /* Clear.                          */
    lcdCommand(0x06);                     /* Increment, no display shift.    */
}

static void lcdSetCursor(uint8_t col, uint8_t row)
{
    lcdCommand((uint8_t)(0x80u | ((row != 0u) ? 0x40u : 0x00u) | col));
}

static void lcdPrint(const char *s)
{
    while (*s != '\0')
    {
        lcdWrite((uint8_t)*s++, 1);
    }
}

/* ---- ADC ---------------------------------------------------------------- */

static uint16_t adcRead(uint8_t channel)
{
    ADMUX  = _BV(REFS0) | (channel & 0x07u);              /* AVCC reference. */
    ADCSRA = _BV(ADEN) | _BV(ADSC) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
    while (ADCSRA & _BV(ADSC))
    {
        /* Wait for the end of the conversion. */
    }
    return ADC;
}

int main(void)
{
    lcdInit();
    lcdPrint("Bonjour STI2D");

    for (;;)
    {
        /* A0 in hundredths of a volt: 0..500. */
        const uint16_t cV = (uint16_t)(((uint32_t)adcRead(0) * 500u + 512u) / 1024u);
        char text[] = "A0 = 0.00 V";
        text[5] = (char)('0' + cV / 100u);
        text[7] = (char)('0' + (cV / 10u) % 10u);
        text[8] = (char)('0' + cV % 10u);
        lcdSetCursor(0, 1);
        lcdPrint(text);
        _delay_ms(2);
    }
}
