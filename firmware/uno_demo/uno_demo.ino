// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of uno_demo.c (board: Arduino Uno).
// Blinks the "L" LED (D13) and answers on the serial port (115200 bauds):
//   "BLINK OFF" / "BLINK ON" stop / restart blinking, other lines are echoed.
// Export with "Sketch > Export compiled binary".

const uint8_t       kLedPin        = LED_BUILTIN;    // D13 = PB5
const unsigned long kHalfPeriodMs  = 1;              // 500 on a real board

bool          blinking = true;
unsigned long lastToggle = 0;
String        line;

void execute(const String &command)
{
    if (command == "BLINK OFF")
    {
        blinking = false;
        digitalWrite(kLedPin, LOW);
        Serial.println("OK BLINK OFF");
    }
    else if (command == "BLINK ON")
    {
        blinking = true;
        Serial.println("OK BLINK ON");
    }
    else
    {
        Serial.print("Recu : ");
        Serial.println(command);
    }
}

void setup()
{
    pinMode(kLedPin, OUTPUT);
    Serial.begin(115200);
    Serial.println("Arduino UNO pret");
}

void loop()
{
    // Blink without delay(): the serial port is read in between.
    const unsigned long now = millis();
    if (blinking && (now - lastToggle >= kHalfPeriodMs))
    {
        lastToggle = now;
        digitalWrite(kLedPin, !digitalRead(kLedPin));
    }

    while (Serial.available() > 0)
    {
        const char c = static_cast<char>(Serial.read());
        if (c == '\n')
        {
            execute(line);
            line = "";
        }
        else if (c != '\r')
        {
            line += c;
        }
    }
}
