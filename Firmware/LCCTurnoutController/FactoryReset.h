#ifndef __FACTORY_RESET_H
#define __FACTORY_RESET_H

/// @file FactoryReset.h
/// Hardware factory-reset button handling.

/// Check whether the factory-reset button (FACTORY_RESET_PIN) is held at
/// startup.  If pressed for FACTORY_RESET_HOLD_MS the built-in LED flashes
/// 5 times over 5 seconds, then SPIFFS and the NVS turnout namespace are
/// wiped and the board reboots.
///
/// @return true if a reset was triggered (the board will reboot before this
///         actually returns).  false if no reset was requested.
bool check_factory_reset_button();

#endif // __FACTORY_RESET_H
