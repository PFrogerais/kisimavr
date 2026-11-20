// SPDX-License-Identifier: GPL-3.0-or-later
// Arduino version of uart_cmd.c (board: Arduino Uno).
// Commands at 115200 bauds, ended by '\n': "LED ON", "LED OFF".
// Export with "Sketch > Export compiled binary".

const uint8_t kLedPin = 13;    // D13 = PB5

void setup()
{
    pinMode(kLedPin, OUTPUT);
    Serial.begin(115200);
    Serial.println("Pret");
}

void loop()
{
    if (Serial.available() == 0)
    {
        return;
    }
    String line = Serial.readStringUntil('\n');    // Waits up to 1 s (simulated)
    line.trim();                                   // Drops '\r' and spaces

    if (line == "LED ON")
    {
        digitalWrite(kLedPin, HIGH);
        Serial.println("OK LED ON");
    }
    else if (line == "LED OFF")
    {
        digitalWrite(kLedPin, LOW);
        Serial.println("OK LED OFF");
    }
    else
    {
        Serial.print("Recu : ");
        Serial.println(line);
    }
}
