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
 * Build: make uart_cmd.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>
#include <string.h>

#define LINE_MAX 40u

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);                      /* 16 MHz / 8 / 17 = ~115200 bauds. */
    UBRR0  = 16;
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
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
    while (!(UCSR0A & _BV(RXC0)))
    {
        /* Wait for a received byte. */
    }
    return (char)UDR0;
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
