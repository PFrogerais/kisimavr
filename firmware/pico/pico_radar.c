/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_radar.c
 * @brief Raspberry Pi Pico: HC-SR04 distance sensor and servo following it.
 *
 *   GP2 -> TRIG of the HC-SR04 (12 us pulse every 60 ms)
 *   GP3 <- ECHO, through a 1k / 2k divider (5 V module, 3.3 V input)
 *   GP15 -> servo signal, PWM 50 Hz (slice 7, channel B)
 * 5 cm -> 0 deg, 55 cm -> 180 deg (pulse 544..2400 us).
 * UART0 115200 bauds: "Distance = 40.0 cm, servo 126 deg".
 *
 * Build: make -C firmware/pico (clang + lld) -> pico_radar.bin
 */

#include "pico_hw.h"

#define TRIG    2u
#define ECHO    3u
#define SERVO   15u

/* Echo width in us (0: none). */
static uint32_t measureUs(void)
{
    REG(SIO_OUT_SET) = 1u << TRIG;
    delayUs(12u);
    REG(SIO_OUT_CLR) = 1u << TRIG;

    const uint32_t sent = REG(TIMERAWL);
    while (!(REG(SIO_IN) & (1u << ECHO)))
    {
        if (REG(TIMERAWL) - sent > 4000u)
        {
            return 0u;                                   /* No echo started */
        }
    }
    const uint32_t start = REG(TIMERAWL);
    while (REG(SIO_IN) & (1u << ECHO))
    {
        if (REG(TIMERAWL) - start > 30000u)
        {
            return 0u;                                   /* Out of range (38 ms) */
        }
    }
    return REG(TIMERAWL) - start;
}

int main(void)
{
    clearBss();
    uartInit();
    REG(IO_CTRL(TRIG)) = FUNC_SIO;
    REG(SIO_OE_SET)    = 1u << TRIG;
    REG(IO_CTRL(ECHO)) = FUNC_SIO;
    REG(PADS(ECHO))    = PADS_IE;
    pwmInit(SERVO, 20000u);                              /* 50 Hz */
    pwmSet(SERVO, 1472u);                                /* 90 deg */
    uartPuts("Radar : HC-SR04 + servo\r\n");

    uint32_t next = REG(TIMERAWL);
    for (;;)
    {
        const uint32_t us = measureUs();
        const uint32_t tenths = us * 10u / 58u;          /* 0.1 cm */
        uint32_t span = tenths < 50u ? 0u : tenths - 50u;
        if (span > 500u)
        {
            span = 500u;
        }
        const uint32_t degrees = span * 180u / 500u;
        pwmSet(SERVO, 544u + degrees * (2400u - 544u) / 180u);

        if (us == 0u)
        {
            uartPuts("Distance : hors de portee\r\n");
        }
        else
        {
            uartPuts("Distance = ");
            uartDec(tenths / 10u);
            uartPutc('.');
            uartDec(tenths % 10u);
            uartPuts(" cm, servo ");
            uartDec(degrees);
            uartPuts(" deg\r\n");
        }
        next += 60000u;                                  /* One measure every 60 ms */
        while ((int32_t)(REG(TIMERAWL) - next) < 0)
        {
            /* Wait. */
        }
    }
}
