// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of i2c_lm75.c (board: Arduino Uno).
// I2C on A4 (SDA) / A5 (SCL): LM75 at 0x48, 24C02 EEPROM at 0x50.
// Export with "Sketch > Export compiled binary".

#include <Wire.h>

const uint8_t kLm75   = 0x48;
const uint8_t kEeprom = 0x50;

float readLm75()
{
    Wire.beginTransmission(kLm75);
    Wire.write(0x00);                      // Temperature register
    if (Wire.endTransmission(false) != 0)  // Repeated START
    {
        return NAN;
    }
    Wire.requestFrom(kLm75, (uint8_t)2);
    const int16_t raw = (int16_t)((Wire.read() << 8) | Wire.read());
    return (raw >> 7) * 0.5f;              // 9 bits, 0.5 degC
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();                          // 100 kHz, internal pull-ups on

    Wire.beginTransmission(kEeprom);       // Write "STI2D" at 0x00
    Wire.write(0x00);
    Wire.write("STI2D");
    Wire.endTransmission();

    Wire.beginTransmission(kEeprom);       // Read it back
    Wire.write(0x00);
    Wire.endTransmission(false);
    Wire.requestFrom(kEeprom, (uint8_t)5);
    Serial.print("EEPROM : ");
    while (Wire.available())
    {
        Serial.write(Wire.read());
    }
    Serial.println();
}

void loop()
{
    Serial.print("T = ");
    Serial.print(readLm75(), 1);
    Serial.println(" C");
}
