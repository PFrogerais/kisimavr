/*
 * kisimavr - ATmega co-simulation for ngspice / KiCad
 * Copyright (C) 2026 Paul
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/**
 * @file pico_ventilo.c
 * @brief Raspberry Pi Pico fan regulation: NTC on ADC0, DC motor through an L298N.
 *
 *   GP26 (ADC0) <- divider 10 kohm (to 3.3 V) / NTC 10 kohm B = 3950 (to GND)
 *   GP16 -> ENA of the L298N, PWM 1 kHz (slice 0, channel A)
 *   GP17 -> IN1 = 1, GP18 -> IN2 = 0: one direction
 * Below 25 degC the motor stops; from 25 to 35 degC the duty cycle goes
 * from 30 to 100 %. UART0 115200 bauds every 20 ms: "T = 27.4 C, moteur 51 %".
 *
 * Build: make -C firmware/pico (clang + lld) -> pico_ventilo.bin
 */

#include "pico_hw.h"

#define ENA     16u
#define IN1     17u
#define IN2     18u

/* NTC 10 kohm B = 3950: resistance (ohm) from -10 to 80 degC, 5 degC steps. */
static const uint32_t kNtc[] = { 58246u, 44026u, 33621u, 25925u, 20175u, 15837u, 12535u,
                                 10000u, 8037u, 6506u, 5301u, 4348u, 3588u, 2978u, 2486u,
                                 2086u, 1760u, 1492u, 1270u };
#define NTC_COUNT (sizeof kNtc / sizeof kNtc[0])

/* Temperature in tenths of degC + 100 from the NTC resistance. */
static uint32_t ntcTenths(uint32_t ohm)
{
    if (ohm >= kNtc[0])
    {
        return 0u;
    }
    for (uint32_t i = 0; i + 1u < NTC_COUNT; ++i)
    {
        if (ohm > kNtc[i + 1u])
        {
            return 50u * i + 50u * (kNtc[i] - ohm) / (kNtc[i] - kNtc[i + 1u]);
        }
    }
    return 50u * (NTC_COUNT - 1u);
}

int main(void)
{
    clearBss();
    uartInit();
    REG(IO_CTRL(IN1)) = FUNC_SIO;
    REG(IO_CTRL(IN2)) = FUNC_SIO;
    REG(SIO_OE_SET)   = (1u << IN1) | (1u << IN2);
    REG(SIO_OUT_SET)  = 1u << IN1;
    REG(PADS(26))     = 0u;                              /* Analog input */
    pwmInit(ENA, 1000u);                                 /* 1 kHz */
    uartPuts("Ventilateur : CTN + L298N\r\n");

    uint32_t next = REG(TIMERAWL);
    for (;;)
    {
        const uint32_t adc = adcRead(0u);
        const uint32_t ohm = adc >= 4095u ? 100000u : 10000u * adc / (4095u - adc);
        const uint32_t t = ntcTenths(ohm);               /* tenths + 100 */
        uint32_t duty = 0u;
        if (t >= 450u)
        {
            duty = 100u;
        }
        else if (t >= 350u)
        {
            duty = 30u + (t - 350u) * 70u / 100u;
        }
        pwmSet(ENA, duty * 10u);

        uartPuts("T = ");
        if (t < 100u)
        {
            uartPutc('-');
            uartDec((100u - t) / 10u);
            uartPutc('.');
            uartDec((100u - t) % 10u);
        }
        else
        {
            uartDec((t - 100u) / 10u);
            uartPutc('.');
            uartDec((t - 100u) % 10u);
        }
        uartPuts(" C, moteur ");
        uartDec(duty);
        uartPuts(" %\r\n");
        next += 20000u;
        while ((int32_t)(REG(TIMERAWL) - next) < 0)
        {
            /* Wait. */
        }
    }
}
