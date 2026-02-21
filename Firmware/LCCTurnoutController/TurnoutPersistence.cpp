#include "TurnoutPersistence.h"
#include "HardwareDefs.h"

#include <Preferences.h>
#include <SPIFFS.h>
#include <OpenMRNLite.h>
#include <fcntl.h>

#include "ServoTurnout.h"

// ── save_turnout_state ──────────────────────────────────────────────

void save_turnout_state(uint8_t index, bool state) {
    char key[4];
    snprintf(key, sizeof(key), "t%u", index);
    turnoutPrefs.putBool(key, state);
}

// ── detect_config_version_mismatch ──────────────────────────────────

static constexpr openlcb::ConfigDef persistCfg(0);

bool detect_config_version_mismatch() {
    Serial.printf("Checking config file (expecting version 0x%04X)...\n",
                  openlcb::CANONICAL_VERSION);

    int fd = ::open(openlcb::CONFIG_FILENAME, O_RDONLY);
    if (fd >= 0) {
        uint16_t stored =
            persistCfg.seg().internal_config().version().read(fd);
        ::close(fd);
        Serial.printf("Current stored version: 0x%04X\n", stored);
        return stored != openlcb::CANONICAL_VERSION;
    }

    Serial.println("Config file does not exist yet");
    return true;
}

// ── restore_turnout_states ──────────────────────────────────────────

void restore_turnout_states(bool needsFactoryReset) {
    turnoutPrefs.begin("turnout", false);

    if (needsFactoryReset) {
        Serial.println(
            "Factory reset detected \u2014 clearing persisted turnout state");
        turnoutPrefs.clear();
    }

    for (uint8_t i = 0; i < NUM_TURNOUTS; i++) {
        servoTurnouts[i]->set_state_callback(i, save_turnout_state);

        char key[4];
        snprintf(key, sizeof(key), "t%u", i);
        if (turnoutPrefs.isKey(key)) {
            bool saved = turnoutPrefs.getBool(key, false);
            servoTurnouts[i]->set_pending_restore(saved);
            Serial.printf("Turnout %u: will restore to %s\n", i,
                          saved ? "NORMAL" : "REVERSED");
        }
    }
}
