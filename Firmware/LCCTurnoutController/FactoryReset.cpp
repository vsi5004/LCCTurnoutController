#include "FactoryReset.h"
#include "HardwareDefs.h"

#include <SPIFFS.h>
#include <Preferences.h>

bool check_factory_reset_button() {
    pinMode(FACTORY_RESET_PIN, INPUT);  // external pull-up on the board
    if (digitalRead(FACTORY_RESET_PIN) != ARDUINO_LOW) {
        return false;  // button not pressed
    }

    Serial.println(
        "Factory reset button detected \u2014 hold for 5 s to confirm...");
    unsigned long start = millis();
    while (millis() - start < FACTORY_RESET_HOLD_MS) {
        if (digitalRead(FACTORY_RESET_PIN) != ARDUINO_LOW) {
            Serial.println("Button released \u2014 factory reset cancelled");
            return false;
        }
        delay(50);
    }

    // Button held the full duration \u2014 flash LED 5 times in 5 s then reset.
    Serial.println("Factory reset confirmed! Flashing LED...");
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_BUILTIN, ARDUINO_HIGH);
        delay(500);
        digitalWrite(LED_BUILTIN, ARDUINO_LOW);
        delay(500);
    }

    // Wipe SPIFFS (CDI config) and NVS turnout state.
    Serial.println("Erasing config and turnout state...");
    SPIFFS.format();
    Preferences prefs;
    prefs.begin("turnout", false);
    prefs.clear();
    prefs.end();

    Serial.println("Factory reset complete \u2014 rebooting");
    delay(500);
    ESP.restart();
    return true;  // unreachable, keeps the compiler happy
}
