/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico2_demo.c
 * @brief Raspberry Pi Pico 2 (RP2350) demo, bare metal (no SDK): GPIO, UART, ADC.
 *
 * Same behaviour as firmware/pico/pico_demo.c (Pico, RP2040):
 * - GP25 (on-board LED) and GP15: square wave, 1 kHz (toggled every 500 us).
 * - GP14: push-button to GND (internal pull-up); GP16 = LED lit while pressed.
 * - UART0 115200 bauds 8N1 on GP0 (TX) / GP1 (RX): "Pico 2 pret (RISC-V)",
 *   then every 4 ms "ADC0 = 2048 (1.65 V)" (GP26) and
 *   "VSYS = 4.72 V, USB : oui" (ADC3 = GP29 reads VSYS / 3 on the board,
 *   GP24 senses VBUS), and "Recu : <line>" for every line received.
 *
 * One source, two builds (make -C firmware/pico2, clang + lld):
 *   pico2_demo_riscv.bin  Hazard3 RISC-V cores (rv32imac)
 *   pico2_demo_arm.bin    Cortex-M33 cores (thumbv8m.main)
 * The A2 bootrom starts the image as on the chip: it finds the IMAGE_DEF
 * block below, which tells the core architecture. The clocks are those left
 * by the simulation (clk_sys = clk_peri = 150 MHz): on a real board, the
 * Pico SDK or Arduino-pico set them up, and their .uf2 files run the same
 * way in the simulation.
 */

#include <stdbool.h>
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* ---- RP2350 registers ------------------------------------------------- */
#define RESETS_RESET    0x40020000u
#define RESETS_DONE     0x40020008u
#define RESETS_CLR      0x3000u                      /* Atomic clear alias   */
#define RST_ADC         (1u << 0)
#define RST_IO_BANK0    (1u << 6)
#define RST_PADS_BANK0  (1u << 9)
#define RST_TIMER0      (1u << 23)
#define RST_UART0       (1u << 26)

#define IO_CTRL(n)      (0x40028004u + 8u * (n))     /* IO_BANK0 GPIOn_CTRL  */
#define PADS(n)         (0x40038004u + 4u * (n))     /* PADS_BANK0 GPIOn     */
#define PADS_IE         (1u << 6)
#define PADS_PUE        (1u << 3)
#define PADS_PDE        (1u << 2)                    /* ISO (bit 8) cleared  */
#define FUNC_UART       2u
#define FUNC_SIO        5u

#define SIO_IN          0xd0000004u
#define SIO_OUT_SET     0xd0000018u
#define SIO_OUT_CLR     0xd0000020u
#define SIO_OUT_XOR     0xd0000028u
#define SIO_OE_SET      0xd0000038u

#define TIMERAWL        0x400b0028u                  /* TIMER0, microseconds */

#define UART0           0x40070000u
#define UART_DR         (UART0 + 0x000u)
#define UART_FR         (UART0 + 0x018u)
#define UART_IBRD       (UART0 + 0x024u)
#define UART_FBRD       (UART0 + 0x028u)
#define UART_LCR_H      (UART0 + 0x02cu)
#define UART_CR         (UART0 + 0x030u)
#define UART_FR_TXFF    (1u << 5)
#define UART_FR_RXFE    (1u << 4)

#define ADC_CS          0x400a0000u
#define ADC_RESULT      0x400a0004u
#define ADC_CS_EN       (1u << 0)
#define ADC_CS_START    (1u << 2)
#define ADC_CS_READY    (1u << 8)

/* ---- Start-up and IMAGE_DEF block (RP2350 datasheet, 5.9 "Boot") ------ */
int main(void);
extern uint32_t _bss_start, _bss_end, _stack_top;

#define PICOBIN_START   0xffffded3u
#define PICOBIN_END     0xab123579u

#if defined(__riscv)
#define ARCH_NAME "RISC-V"

/* The bootrom jumps to the ENTRY_POINT item, at the start of the flash. */
__attribute__((section(".entry"), naked, noreturn)) void _start(void)
{
    __asm__ volatile("la sp, _stack_top\n"
                     "call main\n"
                     "1: j 1b\n");
}

/* IMAGE_TYPE: executable, RISC-V, RP2350; ENTRY_POINT: entry and stack. */
__attribute__((section(".image_def"), used)) static const uint32_t imageDef[] = {
    PICOBIN_START,
    0x11010142u,                   /* IMAGE_TYPE, 1 word: EXE | CPU_RISCV | CHIP_RP2350 */
    0x00000344u,                   /* ENTRY_POINT, 3 words                 */
    (uint32_t)&_start,
    (uint32_t)&_stack_top,
    0x000004ffu,                   /* LAST, items = 4 words                */
    0u,                            /* Next block: this one                 */
    PICOBIN_END,
};

#elif defined(__ARM_ARCH_8M_MAIN__)
#define ARCH_NAME "ARM"

void _start(void);

/* The bootrom takes the stack and the entry from the vector table. */
__attribute__((section(".entry"), used)) static void *const vectors[] = {
    &_stack_top,
    (void *)_start,
};

/* IMAGE_TYPE: executable, Arm secure, RP2350. */
__attribute__((section(".image_def"), used)) static const uint32_t imageDef[] = {
    PICOBIN_START,
    0x10210142u,                   /* IMAGE_TYPE, 1 word: EXE | S | CPU_ARM | CHIP_RP2350 */
    0x000001ffu,                   /* LAST, items = 1 word                 */
    0u,                            /* Next block: this one                 */
    PICOBIN_END,
};

__attribute__((noreturn)) void _start(void)
{
    main();
    for (;;)
    {
    }
}

#else
#error "build for riscv32 (Hazard3) or thumbv8m.main (Cortex-M33)"
#endif

static void clearBss(void)
{
    for (uint32_t *p = &_bss_start; p < &_bss_end; ++p)
    {
        *p = 0u;
    }
}

static void unreset(uint32_t blocks)
{
    REG(RESETS_RESET + RESETS_CLR) = blocks;
    while ((REG(RESETS_DONE) & blocks) != blocks)
    {
        /* Wait for the blocks to leave reset. */
    }
}

/* ---- UART0 ------------------------------------------------------------ */
static void uartInit(void)
{
    REG(IO_CTRL(0)) = FUNC_UART;                     /* GP0 = TX, GP1 = RX   */
    REG(IO_CTRL(1)) = FUNC_UART;
    REG(PADS(0))    = 0u;
    REG(PADS(1))    = PADS_IE | PADS_PUE;
    REG(UART_IBRD)  = 81u;                           /* 150 MHz / (16 x 115200) */
    REG(UART_FBRD)  = 24u;
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
    unreset(RST_IO_BANK0 | RST_PADS_BANK0 | RST_UART0 | RST_ADC | RST_TIMER0);

    REG(IO_CTRL(25)) = FUNC_SIO;                     /* LED and square wave  */
    REG(IO_CTRL(15)) = FUNC_SIO;
    REG(IO_CTRL(16)) = FUNC_SIO;
    REG(PADS(25))    = 0u;
    REG(PADS(15))    = 0u;
    REG(PADS(16))    = 0u;
    REG(SIO_OE_SET)  = (1u << 25) | (1u << 15) | (1u << 16);
    REG(IO_CTRL(14)) = FUNC_SIO;                     /* Button, pull-up      */
    REG(PADS(14))    = PADS_IE | PADS_PUE;
    REG(PADS(26))    = 0u;                           /* Analog inputs        */
    REG(PADS(29))    = 0u;
    REG(IO_CTRL(24)) = FUNC_SIO;                     /* VBUS sense: input    */
    REG(PADS(24))    = PADS_IE;

    uartInit();
    uartPuts("Pico 2 pret (" ARCH_NAME ")\r\n");

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
