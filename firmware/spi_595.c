/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file spi_595.c
 * @brief SPI demo: 8 LEDs chaser on a 74HC595 shift register.
 *
 * SPI master, mode 0, MSB first, fosc/16 = 1 MHz:
 *   D13 (PB5) SCK  -> SRCLK (11)     D11 (PB3) MOSI -> SER (14)
 *   D10 (PB2) SS   -> RCLK  (12)     D12 (PB4) MISO <- QH' (9)
 * A rising edge on SS (RCLK) copies the shifted byte to QA..QH. While a new
 * byte is shifted in, the previous one comes out on QH': MISO reads it
 * back, which checks the whole loop.
 *
 * Serial 115200 bauds: "Q = 04  relu = 02" for each step.
 *
 * Build: make spi_595.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>
#include <util/delay.h>

static void spiInit(void)
{
    DDRB |= _BV(DDB5) | _BV(DDB3) | _BV(DDB2);    /* SCK, MOSI, SS outputs. */
    PORTB |= _BV(PORTB2);                         /* SS (RCLK) idle high. */
    SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR0);      /* Master, mode 0, fosc/16. */
}

/* One byte out on MOSI, one byte in from MISO. */
static uint8_t spiTransfer(uint8_t value)
{
    SPDR = value;
    while (!(SPSR & _BV(SPIF)))
    {
        /* Wait for the end of the 8 clock periods. */
    }
    return SPDR;
}

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

static void uartHex(uint8_t value)
{
    static const char kDigits[] = "0123456789ABCDEF";
    uartPutc(kDigits[value >> 4]);
    uartPutc(kDigits[value & 0x0Fu]);
}

int main(void)
{
    uint8_t pattern = 0x01;

    spiInit();
    uartInit();
    uartPuts("SPI 74HC595 : chenillard\r\n");

    for (;;)
    {
        PORTB &= (uint8_t)~_BV(PORTB2);           /* RCLK low...              */
        const uint8_t back = spiTransfer(pattern);
        PORTB |= _BV(PORTB2);                     /* ...rising edge: latch.   */

        uartPuts("Q = ");
        uartHex(pattern);
        uartPuts("  relu = ");
        uartHex(back);
        uartPuts("\r\n");                         /* About 1.6 ms at 115200.  */
        _delay_us(400);

        pattern = (uint8_t)((pattern << 1) | (pattern >> 7));    /* Rotate left. */
    }
}
