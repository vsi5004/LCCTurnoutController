#ifndef __TURNOUT_PERSISTENCE_H
#define __TURNOUT_PERSISTENCE_H

#include <cstdint>

/// @file TurnoutPersistence.h
/// NVS helpers for saving and restoring turnout positions.

/// Save a single turnout's state to NVS.
/// Called by the ServoGPIOBit state-change callback on every move.
void save_turnout_state(uint8_t index, bool state);

/// Detect whether the stored CDI config version differs from
/// CANONICAL_VERSION (indicating a factory reset occurred).
/// Must be called *before* create_config_file_if_needed().
bool detect_config_version_mismatch();

/// Open the NVS "turnout" namespace, clear it if a factory reset was
/// detected, and queue pending restores on each ServoTurnout.
/// Call after hardware and ServoTurnout objects are created.
void restore_turnout_states(bool needsFactoryReset);

#endif // __TURNOUT_PERSISTENCE_H
