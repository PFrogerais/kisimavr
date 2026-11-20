/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_demo.c
 * @brief Raspberry Pi Pico demo, bare metal (no SDK): GPIO, UART, ADC.
 *
 * - GP25 (on-board LED) and GP15: square wave, 1 kHz (toggled every 500 us).
 * - GP14: push-button to GND (internal pull-up); GP16 = LED lit while pressed.
 * - UART0 115200 bauds 8N1 on GP0 (TX) / GP1 (RX): "Pico pret", then every
 *   4 ms "ADC0 = 2048 (1.65 V)" (GP26) and "VSYS = 4.72 V, USB : oui"
 *   (ADC3 = GP29 reads VSYS / 3 on the board, GP24 senses VBUS), and
 *   "Recu : <line>" for every line received.
 *
 * Build: make -C firmware/pico   (clang + lld, thumbv6m), gives pico_demo.bin.
 * Real Pico projects use the Pico SDK or Arduino-pico: their .uf2 files run
 * the same way in the simulation.
 */

#include <stdbool.h>
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- RP2040 registers ------------------------------------------------- */
#define IO_CTRL(n)      (0x40014004u + 8u * (n))     /* IO_BANK0 GPIOn_CTRL  */
#define PADS(n)         (0x4001c004u + 4u * (n))     /* PADS_BANK0 GPIOn     */
#define PADS_IE         (1u << 6)
#define PADS_PUE        (1u << 3)
#define PADS_PDE        (1u << 2)
#define FUNC_UART       2u
#define FUNC_SIO        5u

#define SIO_IN          0xd0000004u
#define SIO_OUT_SET     0xd0000014u
#define SIO_OUT_CLR     0xd0000018u
#define SIO_OUT_XOR     0xd000001cu
#define SIO_OE_SET      0xd0000024u

#define TIMERAWL        0x40054028u                  /* Microseconds.        */

#define UART0           0x40034000u
#define UART_DR         (UART0 + 0x000u)
#define UART_FR         (UART0 + 0x018u)
#define UART_IBRD       (UART0 + 0x024u)
#define UART_FBRD       (UART0 + 0x028u)
#define UART_LCR_H      (UART0 + 0x02cu)
#define UART_CR         (UART0 + 0x030u)
#define UART_FR_TXFF    (1u << 5)
#define UART_FR_RXFE    (1u << 4)

#define ADC_CS          0x4004c000u
#define ADC_RESULT      0x4004c004u
#define ADC_CS_EN       (1u << 0)
#define ADC_CS_START    (1u << 2)
#define ADC_CS_READY    (1u << 8)

/* ---- Start-up: the simulation starts at the beginning of the flash ---- */
int main(void);
extern uint32_t _bss_start, _bss_end;

__attribute__((section(".entry"), naked, noreturn)) void _start(void)
{
    __asm__ volatile("ldr r0, =_stack_top\n"
                     "mov sp, r0\n"
                     "bl main\n"
                     "1: b 1b\n");
}

static void clearBss(void)
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

/* ---- UART0 ------------------------------------------------------------ */
static void uartInit(void)
{
    REG(IO_CTRL(0)) = FUNC_UART;                     /* GP0 = TX, GP1 = RX   */
    REG(IO_CTRL(1)) = FUNC_UART;
    REG(PADS(1))    = PADS_IE | PADS_PUE;
    REG(UART_IBRD)  = 67u;                           /* 125 MHz / (16 x 115200) */
    REG(UART_FBRD)  = 52u;
    REG(UART_LCR_H) = (3u << 5) | (1u << 4);         /* 8 bits, FIFO on      */
    REG(UART_CR)    = (1u << 9) | (1u << 8) | 1u;    /* RXE, TXE, UARTEN     */
}

static void uartPutc(char c)
{
    while (REG(UART_FR) & UART_FR_TXFF)
    {
        /* Wait for room in the FIFO. */
    }
    REG(UART_DR) = (uint8_t)c;
}

static void uartPuts(const char *s)
{
    while (*s != '\0')
    {
        uartPutc(*s++);
    }
}

static void uartDec(uint32_t value, int digits)
{
    char buf[10];
    for (int i = digits - 1; i >= 0; --i)
    {
        buf[i] = (char)('0' + value % 10u);
        value /= 10u;
    }
    for (int i = 0; i < digits; ++i)
    {
        uartPutc(buf[i]);
    }
}

/* ---- ADC -------------------------------------------------------------- */
static uint32_t adcRead(uint32_t channel)
{
    REG(ADC_CS) = ADC_CS_EN | (channel << 12);
    REG(ADC_CS) = ADC_CS_EN | (channel << 12) | ADC_CS_START;
    while (!(REG(ADC_CS) & ADC_CS_READY))
    {
        /* About 2 us. */
    }
    return REG(ADC_RESULT) & 0xfffu;
}

int main(void)
{
    clearBss();

    REG(IO_CTRL(25)) = FUNC_SIO;                     /* LED and square wave  */
    REG(IO_CTRL(15)) = FUNC_SIO;
    REG(IO_CTRL(16)) = FUNC_SIO;
    REG(SIO_OE_SET)  = (1u << 25) | (1u << 15) | (1u << 16);
    REG(IO_CTRL(14)) = FUNC_SIO;                     /* Button, pull-up      */
    REG(PADS(14))    = PADS_IE | PADS_PUE;
    REG(PADS(26))    = 0u;                           /* Analog inputs        */
    REG(PADS(29))    = 0u;
    REG(IO_CTRL(24)) = FUNC_SIO;                     /* VBUS sense: input    */
    REG(PADS(24))    = PADS_IE;

    uartInit();
    uartPuts("Pico pret\r\n");

    char line[41];
    uint32_t length = 0u;
    uint32_t tick = REG(TIMERAWL);
    uint32_t ticks = 0u;

    for (;;)
    {
        if (REG(TIMERAWL) - tick >= 500u)
        {
            tick += 500u;
            REG(SIO_OUT_XOR) = (1u << 25) | (1u << 15);
            if (++ticks % 8u == 0u)                  /* Every 4 ms           */
            {
                const uint32_t code = adcRead(0u);
                const uint32_t cV = (code * 330u + 2048u) / 4096u;
                uartPuts("ADC0 = ");
                uartDec(code, 4);
                uartPuts(" (");
                uartDec(cV / 100u, 1);
                uartPutc('.');
                uartDec(cV % 100u, 2);
                uartPuts(" V)\r\n");

                /* Board: VSYS / 3 on ADC3 (GP29), VBUS sense on GP24. */
                const uint32_t vsys = (adcRead(3u) * 990u + 2048u) / 4096u;   /* 3 x 3.3 V */
                uartPuts("VSYS = ");
                uartDec(vsys / 100u, 1);
                uartPutc('.');
                uartDec(vsys % 100u, 2);
                uartPuts(" V, USB : ");
                uartPuts((REG(SIO_IN) & (1u << 24)) ? "oui\r\n" : "non\r\n");
            }
        }

        /* GP16 lit while the button (GP14, active low) is pressed. */
        if (REG(SIO_IN) & (1u << 14))
        {
            REG(SIO_OUT_CLR) = 1u << 16;
        }
        else
        {
            REG(SIO_OUT_SET) = 1u << 16;
        }

        /* Echo of the received lines. */
        while (!(REG(UART_FR) & UART_FR_RXFE))
        {
            const char c = (char)(REG(UART_DR) & 0xffu);
            if (c == '\n')
            {
                line[length] = '\0';
                uartPuts("Recu : ");
                uartPuts(line);
                uartPuts("\r\n");
                length = 0u;
            }
            else if (c != '\r' && length < sizeof line - 1u)
            {
                line[length++] = c;
            }
        }
    }
}
