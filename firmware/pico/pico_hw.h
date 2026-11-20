/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_hw.h
 * @brief Bare-metal Raspberry Pi Pico (RP2040) helpers shared by the I2C and
 *        SPI demos: registers, start-up, UART0, microsecond delays.
 *
 * Clocks as left by the simulation: clk_sys = clk_peri = 125 MHz. Include
 * this header in exactly one .c file (it defines _start and the helpers).
 * No initialised global variables: the start-up code does not copy .data.
 */

#ifndef PICO_HW_H
#define PICO_HW_H

#include <stdbool.h>
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- GPIO --------------------------------------------------------------- */
#define IO_CTRL(n)      (0x40014004u + 8u * (n))     /* IO_BANK0 GPIOn_CTRL  */
#define PADS(n)         (0x4001c004u + 4u * (n))     /* PADS_BANK0 GPIOn     */
#define PADS_IE         (1u << 6)
#define PADS_PUE        (1u << 3)
#define FUNC_SPI        1u
#define FUNC_UART       2u
#define FUNC_I2C        3u
#define FUNC_SIO        5u

#define SIO_IN          0xd0000004u
#define SIO_OUT_SET     0xd0000014u
#define SIO_OUT_CLR     0xd0000018u
#define SIO_OUT_XOR     0xd000001cu
#define SIO_OE_SET      0xd0000024u

#define TIMERAWL        0x40054028u                  /* Microseconds.        */

/* ---- UART0 -------------------------------------------------------------- */
#define UART0           0x40034000u
#define UART_DR         (UART0 + 0x000u)
#define UART_FR         (UART0 + 0x018u)
#define UART_IBRD       (UART0 + 0x024u)
#define UART_FBRD       (UART0 + 0x028u)
#define UART_LCR_H      (UART0 + 0x02cu)
#define UART_CR         (UART0 + 0x030u)
#define UART_FR_TXFF    (1u << 5)

/* ---- Start-up: the simulation starts at the beginning of the flash ------ */
int main(void);
extern uint32_t _bss_start, _bss_end;

__attribute__((section(".entry"), naked, noreturn)) void _start(void)
{
    __asm__ volatile("ldr r0, =_stack_top\n"
                     "mov sp, r0\n"
                     "bl main\n"
                     "1: b 1b\n");
}

static inline void clearBss(void)
{
    for (uint32_t *p = &_bss_start; p < &_bss_end; ++p)
    {
        *p = 0u;
    }
}

/* ---- Division helpers called by the compiler (Cortex-M0+: no divide) --- */
static uint32_t udivmod(uint32_t n, uint32_t d, uint32_t *rem)
{
    uint32_t q = 0u;
    uint32_t r = 0u;
    for (int i = 31; i >= 0; --i)
    {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d)
        {
            r -= d;
            q |= 1u << i;
        }
    }
    *rem = r;
    return q;
}

uint32_t __aeabi_uidiv(uint32_t n, uint32_t d);
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d);

uint32_t __aeabi_uidiv(uint32_t n, uint32_t d)
{
    uint32_t r;
    return udivmod(n, d, &r);
}

/* Quotient in r0, remainder in r1 (AEABI). */
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d)
{
    uint32_t r;
    const uint32_t q = udivmod(n, d, &r);
    return ((uint64_t)r << 32) | q;
}

/* ---- Time --------------------------------------------------------------- */
static inline void delayUs(uint32_t us)
{
    const uint32_t start = REG(TIMERAWL);
    while (REG(TIMERAWL) - start < us)
    {
        /* Busy wait. */
    }
}

/* ---- UART0: 115200 bauds 8N1 on GP0 (TX) / GP1 (RX) ---------------------- */
static inline void uartInit(void)
{
    REG(IO_CTRL(0)) = FUNC_UART;
    REG(IO_CTRL(1)) = FUNC_UART;
    REG(PADS(1))    = PADS_IE | PADS_PUE;
    REG(UART_IBRD)  = 67u;                           /* 125 MHz / (16 x 115200) */
    REG(UART_FBRD)  = 52u;
    REG(UART_LCR_H) = (3u << 5) | (1u << 4);         /* 8 bits, FIFO on      */
    REG(UART_CR)    = (1u << 9) | (1u << 8) | 1u;    /* RXE, TXE, UARTEN     */
}

static inline void uartPutc(char c)
{
    while (REG(UART_FR) & UART_FR_TXFF)
    {
        /* Wait for room in the FIFO. */
    }
    REG(UART_DR) = (uint8_t)c;
}

static inline void uartPuts(const char *s)
{
    while (*s != '\0')
    {
        uartPutc(*s++);
    }
}

static inline void uartHex(uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    uartPutc(digits[(value >> 4) & 0xFu]);
    uartPutc(digits[value & 0xFu]);
}

#endif /* PICO_HW_H */
