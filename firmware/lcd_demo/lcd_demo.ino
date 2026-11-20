// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of lcd_demo.c (board: Arduino Uno): character LCD 16x2,
// wiring of the LiquidCrystal examples, voltage on A0 on the second row.
// Export with "Sketch > Export compiled binary".
// Note: LiquidCrystal::begin() waits 50 ms: simulate at least 70 ms.

#include <LiquidCrystal.h>

// RS, E, D4, D5, D6, D7
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);

void setup()
{
    lcd.begin(16, 2);
    lcd.print("Bonjour STI2D");
}

void loop()
{
    const float volts = analogRead(A0) * 5.0f / 1024.0f;
    lcd.setCursor(0, 1);
    lcd.print("A0 = ");
    lcd.print(volts, 2);      // Two decimals: "A0 = 2.50"
    lcd.print(" V");
    delay(2);
}
