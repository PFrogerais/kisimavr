/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file uart_cmd.c
 * @brief UART reception demo: a line-based command interpreter.
 *
 * 115200 bauds 8N1 on RXD (PD0) / TXD (PD1). Commands, ended by '\n'
 * ('\r' is ignored):
 *   LED ON   -> D13 (PB5) high, answers "OK LED ON"
 *   LED OFF  -> D13 low,        answers "OK LED OFF"
 *   other    -> answers "Recu : <line>"
 *
 * Received bytes are stored by the RX interrupt in a ring buffer, as the
 * Arduino Serial class does: the USART itself only holds 2 bytes, so a
 * polling loop busy sending an answer would lose the next incoming line.
 *
 * Build: make uart_cmd.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <string.h>

#define LINE_MAX 40u
#define RX_SIZE  64u                        /* Power of two. */

static volatile uint8_t s_rxBuffer[RX_SIZE];
static volatile uint8_t s_rxHead;           /* Written by the ISR only. */
static volatile uint8_t s_rxTail;           /* Written by main only. */

ISR(USART_RX_vect)
{
    const uint8_t byte = UDR0;
    const uint8_t next = (uint8_t)((s_rxHead + 1u) & (RX_SIZE - 1u));

    if (next != s_rxTail)                    /* Drop the byte when full. */
    {
        s_rxBuffer[s_rxHead] = byte;
        s_rxHead = next;
    }
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
    if (strcmp(line, "LED ON") == 0)
    {
        PORTB |= _BV(PORTB5);
        uartPuts("OK LED ON\r\n");
    }
    else if (strcmp(line, "LED OFF") == 0)
    {
        PORTB &= (uint8_t)~_BV(PORTB5);
        uartPuts("OK LED OFF\r\n");
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
    uartInit();
    sei();
    uartPuts("Pret\r\n");

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
