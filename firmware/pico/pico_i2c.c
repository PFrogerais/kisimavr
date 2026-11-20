/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_i2c.c
 * @brief Raspberry Pi Pico I2C demo, bare metal: LM75 sensor and LCD 16x2 I2C.
 *
 * I2C0 at 100 kHz on GP4 (SDA) and GP5 (SCL), 4.7 kohm pull-ups on the board:
 *   - LM75 at 0x48: pointer 0 written, then 2 bytes read after a repeated
 *     START (temperature, 0.5 degC steps);
 *   - LCD 16x2 with a PCF8574 backpack at 0x27 (LiquidCrystal_I2C wiring:
 *     P0 = RS, P1 = RW, P2 = E, P3 = backlight, P4..P7 = D4..D7), 4-bit mode:
 *     "Pico I2C" on line 1, "T = 25.5 C" on line 2.
 * UART0 115200 bauds on GP0: "LM75 : T = 25.5 C" and the LCD state.
 *
 * Build: make -C firmware/pico (clang + lld) -> pico_i2c.bin
 */

#include "pico_hw.h"

/* ---- I2C0 (Synopsys DW_apb_i2c) ------------------------------------------ */
#define I2C0                0x40044000u
#define IC_CON              (I2C0 + 0x00u)
#define IC_TAR              (I2C0 + 0x04u)
#define IC_DATA_CMD         (I2C0 + 0x10u)
#define IC_SS_SCL_HCNT      (I2C0 + 0x14u)
#define IC_SS_SCL_LCNT      (I2C0 + 0x18u)
#define IC_RAW_INTR_STAT    (I2C0 + 0x34u)
#define IC_CLR_TX_ABRT      (I2C0 + 0x54u)
#define IC_CLR_STOP_DET     (I2C0 + 0x60u)
#define IC_ENABLE           (I2C0 + 0x6cu)
#define IC_TXFLR            (I2C0 + 0x74u)
#define IC_RXFLR            (I2C0 + 0x78u)

#define CMD_READ            (1u << 8)
#define CMD_STOP            (1u << 9)
#define CMD_RESTART         (1u << 10)
#define INTR_TX_ABRT        (1u << 6)
#define INTR_STOP_DET       (1u << 9)

#define LM75_ADDR           0x48u
#define LCD_ADDR            0x27u

static void i2cInit(void)
{
    REG(IO_CTRL(4)) = FUNC_I2C;                          /* GP4 = SDA, GP5 = SCL */
    REG(IO_CTRL(5)) = FUNC_I2C;
    REG(PADS(4))    = PADS_IE | PADS_PUE;
    REG(PADS(5))    = PADS_IE | PADS_PUE;
    REG(IC_ENABLE)  = 0u;
    REG(IC_CON)     = (1u << 0) | (1u << 1) | (1u << 5) | (1u << 6);  /* Master, 100 kHz */
    REG(IC_SS_SCL_LCNT) = 750u;                          /* 6 us at 125 MHz      */
    REG(IC_SS_SCL_HCNT) = 500u;                          /* 4 us                 */
    REG(IC_ENABLE)  = 1u;
}

/* Wait for the end of the transfer: false on NACK (abort). */
static bool i2cEnd(void)
{
    for (;;)
    {
        const uint32_t raw = REG(IC_RAW_INTR_STAT);
        if (raw & INTR_TX_ABRT)
        {
            (void)REG(IC_CLR_TX_ABRT);
            while (!(REG(IC_RAW_INTR_STAT) & INTR_STOP_DET))
            {
                /* The controller sends a STOP after an abort. */
            }
            (void)REG(IC_CLR_STOP_DET);
            return false;
        }
        if (raw & INTR_STOP_DET)
        {
            (void)REG(IC_CLR_STOP_DET);
            return true;
        }
    }
}

static void i2cTarget(uint32_t address)
{
    REG(IC_ENABLE) = 0u;
    REG(IC_TAR)    = address;
    REG(IC_ENABLE) = 1u;
}

/* Bytes written, then STOP. */
static bool i2cWrite(uint32_t address, const uint8_t *data, uint32_t length)
{
    i2cTarget(address);
    for (uint32_t i = 0; i < length; ++i)
    {
        while (REG(IC_TXFLR) >= 16u)
        {
            /* TX FIFO full. */
        }
        REG(IC_DATA_CMD) = data[i] | (i + 1u == length ? CMD_STOP : 0u);
    }
    return i2cEnd();
}

/* Register pointer written, repeated START, bytes read, STOP. */
static bool i2cReadRegister(uint32_t address, uint8_t reg, uint8_t *data, uint32_t length)
{
    i2cTarget(address);
    REG(IC_DATA_CMD) = reg;
    for (uint32_t i = 0; i < length; ++i)
    {
        REG(IC_DATA_CMD) = CMD_READ | (i == 0u ? CMD_RESTART : 0u) |
                           (i + 1u == length ? CMD_STOP : 0u);
    }
    for (uint32_t i = 0; i < length; ++i)
    {
        while (REG(IC_RXFLR) == 0u)
        {
            if (REG(IC_RAW_INTR_STAT) & INTR_TX_ABRT)
            {
                (void)i2cEnd();
                return false;
            }
        }
        data[i] = (uint8_t)REG(IC_DATA_CMD);
    }
    return i2cEnd();
}

/* ---- LCD 16x2 behind a PCF8574 -------------------------------------------- */
#define LCD_RS              0x01u
#define LCD_E               0x04u
#define LCD_BACKLIGHT       0x08u

/* One 4-bit transfer: D7..D4 with E high, then E low (falling edge latches). */
static bool lcdNibble(uint8_t nibble, uint8_t rs)
{
    const uint8_t port = (uint8_t)((nibble << 4) | LCD_BACKLIGHT | rs);
    const uint8_t frame[2] = { (uint8_t)(port | LCD_E), port };
    return i2cWrite(LCD_ADDR, frame, 2u);
}

static bool lcdByte(uint8_t value, uint8_t rs)
{
    const bool ok = lcdNibble(value >> 4, rs) && lcdNibble(value & 0x0Fu, rs);
    delayUs(50u);                                        /* Command time (37 us) */
    return ok;
}

static bool lcdInit(void)
{
    delayUs(1000u);                                      /* Power-on (simulation: short) */
    bool ok = lcdNibble(0x3u, 0u);                       /* 8-bit mode, three times */
    delayUs(4500u);
    ok = ok && lcdNibble(0x3u, 0u);
    delayUs(150u);
    ok = ok && lcdNibble(0x3u, 0u);
    ok = ok && lcdNibble(0x2u, 0u);                      /* 4-bit mode           */
    ok = ok && lcdByte(0x28u, 0u);                       /* 2 lines, 5x8         */
    ok = ok && lcdByte(0x0Cu, 0u);                       /* Display on           */
    ok = ok && lcdByte(0x01u, 0u);                       /* Clear                */
    delayUs(2000u);
    ok = ok && lcdByte(0x06u, 0u);                       /* Entry mode: increment */
    return ok;
}

static void lcdPrint(uint8_t row, const char *text)
{
    (void)lcdByte((uint8_t)(0x80u | (row ? 0x40u : 0x00u)), 0u);
    while (*text != '\0')
    {
        (void)lcdByte((uint8_t)*text++, LCD_RS);
    }
}

int main(void)
{
    clearBss();
    uartInit();
    i2cInit();
    uartPuts("Pico I2C : LM75 0x48, LCD 0x27\r\n");

    /* LM75: 9-bit temperature, left-justified, two's complement. */
    char line[17];
    const char *unknown = "T = --.- C";
    for (uint32_t i = 0; i < sizeof line; ++i)
    {
        line[i] = unknown[i < 11u ? i : 10u];
    }
    uint8_t t[2];
    if (i2cReadRegister(LM75_ADDR, 0x00u, t, 2u))
    {
        const int16_t raw = (int16_t)((t[0] << 8) | t[1]);
        const int32_t halves = raw >> 7;                 /* 0.5 degC steps       */
        const bool negative = halves < 0;
        uint32_t h = (uint32_t)(negative ? -halves : halves);
        uint32_t deg = h / 2u;
        uint32_t pos = 4u;
        if (negative)
        {
            line[pos++] = '-';
        }
        if (deg >= 100u)
        {
            line[pos++] = (char)('0' + deg / 100u);
        }
        if (deg >= 10u)
        {
            line[pos++] = (char)('0' + (deg / 10u) % 10u);
        }
        line[pos++] = (char)('0' + deg % 10u);
        line[pos++] = '.';
        line[pos++] = (h & 1u) ? '5' : '0';
        line[pos++] = ' ';
        line[pos++] = 'C';
        line[pos] = '\0';
        uartPuts("LM75 : ");
        uartPuts(line);
        uartPuts("\r\n");
    }
    else
    {
        uartPuts("LM75 : pas de reponse (NACK)\r\n");
    }

    if (lcdInit())
    {
        lcdPrint(0u, "Pico I2C");
        lcdPrint(1u, line);
        uartPuts("LCD : ecrit\r\n");
    }
    else
    {
        uartPuts("LCD : pas de reponse (NACK)\r\n");
    }

    for (;;)
    {
        REG(SIO_OUT_XOR) = 0u;
    }
}
