/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file i2c_lm75.c
 * @brief I2C demo: LM75 temperature sensor and 24C02 EEPROM on the TWI bus.
 *
 * - SDA = PC4 (A4), SCL = PC5 (A5), 100 kHz, 4.7 kohm pull-ups to 5 V.
 * - At start-up: writes "STI2D" in the EEPROM (address 0x00), reads it back.
 * - Loop: reads the LM75 (0x48) and prints "T = 25.5 C" at 115200 bauds.
 *
 * Build: make i2c_lm75.hex  (avr-gcc, F_CPU = 16 MHz)
 */

#include <avr/io.h>
#include <stdint.h>

#define LM75_ADDR   0x48u
#define EEPROM_ADDR 0x50u

/* ------------------------------------------------------------------ */
/* UART (115200 bauds 8N1)                                              */
/* ------------------------------------------------------------------ */

static void uartInit(void)
{
    UCSR0A = _BV(U2X0);
    UBRR0  = 16;
    UCSR0B = _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
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

static void uartPuti(int16_t value)
{
    char    digits[6];
    uint8_t n = 0;

    if (value < 0)
    {
        uartPutc('-');
        value = (int16_t)-value;
    }
    do
    {
        digits[n++] = (char)('0' + (uint16_t)value % 10u);
        value = (int16_t)((uint16_t)value / 10u);
    } while (value != 0);

    while (n > 0u)
    {
        uartPutc(digits[--n]);
    }
}

/* ------------------------------------------------------------------ */
/* TWI master (100 kHz)                                                 */
/* ------------------------------------------------------------------ */

static void twiInit(void)
{
    TWSR = 0;                                    /* Prescaler 1.            */
    TWBR = (uint8_t)((F_CPU / 100000UL - 16UL) / 2UL);    /* 72 -> 100 kHz. */
    TWCR = _BV(TWEN);
}

static void twiWait(void)
{
    while (!(TWCR & _BV(TWINT)))
    {
        /* Wait for the end of the current bus operation. */
    }
}

/** START (or repeated START) then SLA+R/W. Returns 1 if the slave acked. */
static uint8_t twiStart(uint8_t address, uint8_t read)
{
    TWCR = _BV(TWINT) | _BV(TWSTA) | _BV(TWEN);
    twiWait();
    TWDR = (uint8_t)((address << 1) | (read ? 1u : 0u));
    TWCR = _BV(TWINT) | _BV(TWEN);
    twiWait();
    const uint8_t status = TWSR & 0xF8u;
    return (status == 0x18u) || (status == 0x40u);    /* SLA+W / SLA+R ACK. */
}

static void twiWrite(uint8_t data)
{
    TWDR = data;
    TWCR = _BV(TWINT) | _BV(TWEN);
    twiWait();
}

static uint8_t twiRead(uint8_t ack)
{
    TWCR = _BV(TWINT) | _BV(TWEN) | (ack ? _BV(TWEA) : 0u);
    twiWait();
    return TWDR;
}

static void twiStop(void)
{
    TWCR = _BV(TWINT) | _BV(TWSTO) | _BV(TWEN);
}

/* ------------------------------------------------------------------ */
/* Devices                                                              */
/* ------------------------------------------------------------------ */

/** LM75 temperature in half degrees Celsius (9-bit, 0.5 degC). */
static int16_t lm75ReadHalfDegrees(void)
{
    if (!twiStart(LM75_ADDR, 0))
    {
        twiStop();
        return INT16_MIN;
    }
    twiWrite(0x00);                     /* Pointer: temperature register. */
    twiStart(LM75_ADDR, 1);             /* Repeated START, read.          */
    const uint8_t msb = twiRead(1);
    const uint8_t lsb = twiRead(0);
    twiStop();

    const int16_t raw = (int16_t)(((uint16_t)msb << 8) | lsb);
    return (int16_t)(raw >> 7);         /* Arithmetic shift: sign kept.   */
}

static void eepromWrite(uint8_t address, const char *text)
{
    twiStart(EEPROM_ADDR, 0);
    twiWrite(address);
    while (*text != '\0')
    {
        twiWrite((uint8_t)*text++);
    }
    twiStop();
}

static void eepromRead(uint8_t address, char *buffer, uint8_t length)
{
    twiStart(EEPROM_ADDR, 0);
    twiWrite(address);
    twiStart(EEPROM_ADDR, 1);
    for (uint8_t i = 0; i < length; ++i)
    {
        buffer[i] = (char)twiRead(i + 1u < length);    /* NACK on the last. */
    }
    twiStop();
    buffer[length] = '\0';
}

int main(void)
{
    char text[6];

    uartInit();
    twiInit();

    eepromWrite(0x00, "STI2D");
    eepromRead(0x00, text, 5);
    uartPuts("EEPROM : ");
    uartPuts(text);
    uartPuts("\r\n");

    for (;;)
    {
        const int16_t half = lm75ReadHalfDegrees();
        if (half == INT16_MIN)
        {
            uartPuts("LM75 absent\r\n");
            continue;
        }
        uartPuts("T = ");
        uartPuti((int16_t)(half / 2));
        uartPuts((half & 1) ? ".5 C\r\n" : ".0 C\r\n");
    }
}
