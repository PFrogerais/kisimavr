// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of spi_595.c (board: Arduino Uno): 8 LEDs chaser on a
// 74HC595. D13 SCK -> SRCLK, D11 MOSI -> SER, D10 -> RCLK, D12 MISO <- QH'.
// Export with "Sketch > Export compiled binary".

#include <SPI.h>

const uint8_t kLatchPin = 10;    // D10 = PB2 (SS), to RCLK

uint8_t pattern = 0x01;

void setup()
{
    pinMode(kLatchPin, OUTPUT);
    digitalWrite(kLatchPin, HIGH);
    SPI.begin();
    Serial.begin(115200);
    Serial.println("SPI 74HC595 : chenillard");
}

void loop()
{
    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(kLatchPin, LOW);
    const uint8_t back = SPI.transfer(pattern);    // Previous byte, from QH'
    digitalWrite(kLatchPin, HIGH);                 // Rising edge: latch
    SPI.endTransaction();

    char line[24];
    snprintf(line, sizeof line, "Q = %02X  relu = %02X", pattern, back);
    Serial.println(line);
    delayMicroseconds(400);

    pattern = static_cast<uint8_t>((pattern << 1) | (pattern >> 7));
}
