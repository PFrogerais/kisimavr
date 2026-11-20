// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of full_demo.c (board: Arduino Uno).
// Export with "Sketch > Export compiled binary" and point the
// ATMEGA328P model at full_demo.ino.hex.

const uint8_t kButtonPin = 2;     // D2 = PD2, to GND, internal pull-up
const uint8_t kLedPin    = 13;    // D13 = PB5
const uint8_t kPwmPin    = 6;     // D6 = PD6 (OC0A)

void setup()
{
    pinMode(kButtonPin, INPUT_PULLUP);
    pinMode(kLedPin, OUTPUT);
    Serial.begin(115200);
    Serial.println("ATmega328P full model");
}

void loop()
{
    const bool pressed = (digitalRead(kButtonPin) == LOW);
    const int  a0      = analogRead(A0);

    digitalWrite(kLedPin, pressed ? HIGH : LOW);
    analogWrite(kPwmPin, a0 / 4);

    Serial.print("A0 = ");
    Serial.print(a0);
    Serial.print("  BTN = ");
    Serial.println(pressed ? 1 : 0);
}
