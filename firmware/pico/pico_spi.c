/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_spi.c
 * @brief Raspberry Pi Pico SPI demo, bare metal: 8 LEDs chaser on a 74HC595.
 *
 * SPI0 master, mode 0, MSB first, 1 MHz, 74HC595 powered at 3.3 V:
 *   GP18 SCK -> SRCLK (11)      GP19 TX (MOSI) -> SER (14)
 *   GP17 GPIO -> RCLK (12)      GP16 RX (MISO) <- QH' (9)
 * A rising edge on RCLK copies the shifted byte to QA..QH. While a new
 * byte is shifted in, the previous one comes out on QH': MISO reads it
 * back, which checks the whole loop.
 * UART0 115200 bauds on GP0: "SPI : envoye 04, relu 02" for each step
 * (one step per millisecond).
 *
 * Build: make -C firmware/pico (clang + lld) -> pico_spi.bin
 */

#include "pico_hw.h"

/* ---- SPI0 (ARM PL022) ----------------------------------------------------- */
#define SPI0                0x4003c000u
#define SSPCR0              (SPI0 + 0x000u)
#define SSPCR1              (SPI0 + 0x004u)
#define SSPDR               (SPI0 + 0x008u)
#define SSPSR               (SPI0 + 0x00cu)
#define SSPCPSR             (SPI0 + 0x010u)
#define SSPSR_RNE           (1u << 2)
#define SSPSR_BSY           (1u << 4)

#define LATCH_PIN           17u

static void spiInit(void)
{
    REG(IO_CTRL(16)) = FUNC_SPI;                         /* RX (MISO)            */
    REG(IO_CTRL(18)) = FUNC_SPI;                         /* SCK                  */
    REG(IO_CTRL(19)) = FUNC_SPI;                         /* TX (MOSI)            */
    REG(PADS(16))    = PADS_IE;
    REG(IO_CTRL(LATCH_PIN)) = FUNC_SIO;                  /* RCLK: plain output   */
    REG(SIO_OE_SET)  = 1u << LATCH_PIN;

    REG(SSPCPSR) = 2u;                                   /* 125 MHz / 2          */
    REG(SSPCR0)  = (61u << 8) | 7u;                      /* / 62, mode 0, 8 bits: 1 MHz */
    REG(SSPCR1)  = 1u << 1;                              /* SSE, master          */
}

/* One byte out on MOSI, one byte in from MISO. */
static uint8_t spiTransfer(uint8_t value)
{
    REG(SSPDR) = value;
    while (!(REG(SSPSR) & SSPSR_RNE))
    {
        /* Wait for the 8 clock periods. */
    }
    return (uint8_t)REG(SSPDR);
}

static void latch(void)
{
    REG(SIO_OUT_SET) = 1u << LATCH_PIN;                  /* RCLK rising edge     */
    delayUs(1u);
    REG(SIO_OUT_CLR) = 1u << LATCH_PIN;
}

int main(void)
{
    clearBss();
    uartInit();
    spiInit();
    uartPuts("Pico SPI : 74HC595, chenillard\r\n");

    uint32_t step = 0u;
    for (;;)
    {
        const uint8_t out = (uint8_t)(1u << (step % 8u));
        const uint8_t back = spiTransfer(out);
        latch();
        uartPuts("SPI : envoye ");
        uartHex(out);
        uartPuts(", relu ");
        uartHex(back);
        uartPuts("\r\n");
        ++step;
        delayUs(1000u);
    }
}
