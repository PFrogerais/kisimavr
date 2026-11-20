/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uno_demo.c
 * @brief Arduino UNO demo: blink the "L" LED and talk on the serial port.
 *
 * - D13 (PB5), LED "L": toggles every BLINK_HALF_PERIOD_MS (Timer1 tick).
 * - Serial 115200 bauds 8N1 on D0/RX (PD0) and D1/TX (PD1), the lines going
 *   to the USB-serial converter (ATmega16U2) of the board. Commands, ended
 *   by '\n' ('\r' is ignored):
 *     BLINK OFF -> stops blinking (L off), answers "OK BLINK OFF"
 *     BLINK ON  -> blinks again,           answers "OK BLINK ON"
 *     other     -> answers "Recu : <line>"
 *
 * Build: make uno_demo.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* 1 ms so that a few periods fit in a short simulation; 500 on a real board. */
#define BLINK_HALF_PERIOD_MS 1u

#define LINE_MAX 40u
#define RX_SIZE  64u                        /* Power of two. */

static volatile bool    s_blinking = true;
static volatile uint8_t s_rxBuffer[RX_SIZE];
static volatile uint8_t s_rxHead;           /* Written by the ISR only. */
static volatile uint8_t s_rxTail;           /* Written by main only. */

/* 1 ms tick: blinks the LED. */
ISR(TIMER1_COMPA_vect)
{
    static uint16_t s_ms;

    if (!s_blinking)
    {
        s_ms = 0;
        return;
    }
    if (++s_ms >= BLINK_HALF_PERIOD_MS)
    {
        s_ms = 0;
        PINB = _BV(PINB5);                   /* Writing 1 to PINx toggles PORTx. */
    }
}

/* Received byte: stored in the ring buffer (dropped when full). */
ISR(USART_RX_vect)
{
    const uint8_t byte = UDR0;
    const uint8_t next = (uint8_t)((s_rxHead + 1u) & (RX_SIZE - 1u));

    if (next != s_rxTail)
    {
        s_rxBuffer[s_rxHead] = byte;
        s_rxHead = next;
    }
}

static void timer1Init(void)
{
    /* CTC mode, prescaler 8: 16 MHz / 8 = 2 MHz -> 2000 counts = 1 ms. */
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS11);
    OCR1A  = 1999;
    TIMSK1 = _BV(OCIE1A);
}

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);                      /* 16 MHz / 8 / 17 = ~115200 bauds. */
    UBRR0  = 16;
    UCSR0B = _BV(RXCIE0) | _BV(RXEN0) | _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);      /* 8N1 */
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

static char uartGetc(void)
{
    while (s_rxTail == s_rxHead)
    {
        /* Wait for the ISR to store a byte. */
    }
    const char c = (char)s_rxBuffer[s_rxTail];
    s_rxTail = (uint8_t)((s_rxTail + 1u) & (RX_SIZE - 1u));
    return c;
}

static void execute(const char *line)
{
    if (strcmp(line, "BLINK OFF") == 0)
    {
        s_blinking = false;
        PORTB &= (uint8_t)~_BV(PORTB5);
        uartPuts("OK BLINK OFF\r\n");
    }
    else if (strcmp(line, "BLINK ON") == 0)
    {
        s_blinking = true;
        uartPuts("OK BLINK ON\r\n");
    }
    else
    {
        uartPuts("Recu : ");
        uartPuts(line);
        uartPuts("\r\n");
    }
}

int main(void)
{
    char    line[LINE_MAX + 1u];
    uint8_t length = 0;

    DDRB |= _BV(DDB5);
    timer1Init();
    uartInit();
    sei();
    uartPuts("Arduino UNO pret\r\n");

    for (;;)
    {
        const char c = uartGetc();
        if (c == '\r')
        {
            continue;
        }
        if (c == '\n')
        {
            line[length] = '\0';
            execute(line);
            length = 0;
        }
        else if (length < LINE_MAX)
        {
            line[length++] = c;    /* Longer lines are truncated. */
        }
    }
}
