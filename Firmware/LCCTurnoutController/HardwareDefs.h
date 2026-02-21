#ifndef __HARDWARE_DEFS_H
#define __HARDWARE_DEFS_H

/// @file HardwareDefs.h
/// Pin assignments, hardware constants, and extern declarations for
/// shared peripheral objects.

#include <Arduino.h>
#include "config.h"

// ── Arduino macro overrides ─────────────────────────────────────────
// These must be set before any library that re-defines HIGH / LOW / OUTPUT.
#undef HIGH
#undef LOW
#undef OUTPUT

#define ARDUINO_HIGH  1
#define ARDUINO_LOW   0
#define ARDUINO_OUTPUT 3
#define ARDUINO_INPUT  1

// ── Forward-declare library types so dependents don't need heavy headers ──
class Adafruit_PWMServoDriver;
class Adafruit_MCP23X17;
class Preferences;

// ── Turnout count & servo constants ─────────────────────────────────
static constexpr uint8_t  NUM_TURNOUTS     = openlcb::NUM_TURNOUTS;
static constexpr uint32_t PWM_COUNT_PER_MS = 4096 / 20; // 20 ms period
static constexpr float    SERVO_FREQ       = 50;         // ~50 Hz

// ── MCP23017 frog-relay pin pairs (even / odd) per turnout ──────────
constexpr uint8_t TURNOUT_FROG_PINS[NUM_TURNOUTS][2] = {
    {8, 9}, {10, 11}, {12, 13}, {14, 15},
    {0, 1}, { 2,  3}, { 4,  5}, { 6,  7}
};

// ── Factory-reset button ────────────────────────────────────────────
static constexpr uint8_t  FACTORY_RESET_PIN     = D2;
static constexpr uint16_t FACTORY_RESET_HOLD_MS = 5000;

// ── Shared peripheral instances (defined in the .ino) ───────────────
class PCAPwm;
class MCPGpio;
namespace openlcb { class ServoTurnout; }

extern Adafruit_PWMServoDriver pwm;
extern Adafruit_MCP23X17       mcp;
extern Preferences             turnoutPrefs;

extern PCAPwm              *servoPwmWrappers[NUM_TURNOUTS];
extern MCPGpio             *gpioWrappers[NUM_TURNOUTS];
extern openlcb::ServoTurnout *servoTurnouts[NUM_TURNOUTS];

// ── OpenMRN file-path symbols (defined in the .ino) ─────────────────
namespace openlcb {
extern const char CDI_FILENAME[];
extern const char *const CONFIG_FILENAME;
extern const size_t CONFIG_FILE_SIZE;
}

#endif // __HARDWARE_DEFS_H
