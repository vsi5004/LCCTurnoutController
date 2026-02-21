# LCC Turnout Controller — Firmware

Arduino sketch for the LCC Turnout Controller. Built on [OpenMRNLite](https://github.com/bakerstu/openmrn) for the ESP32 Arduino core.

## Prerequisites

- **Arduino IDE 2.x** (or PlatformIO)
- **ESP32 Arduino core 3.2.1** (ESP-IDF v5.4) — board package for the Seeed XIAO ESP32 series
- **Libraries** (install via the Arduino Library Manager):
  - `OpenMRNLite` 2.2.1
  - `Adafruit PWM Servo Driver Library` 3.0.2 (PCA9685)
  - `Adafruit MCP23017 Arduino Library` 2.3.2 (MCP23X17)

## Building

1. Open `LCCTurnoutController/LCCTurnoutController.ino` in the Arduino IDE.
2. Select the appropriate board (e.g. *XIAO_ESP32S3*).
3. Compile and upload.

On first boot the firmware will format SPIFFS (if needed) and generate a CDI configuration file at `/spiffs/openlcb_config`.

## File Overview

| File | Purpose |
|------|---------|
| `LCCTurnoutController.ino` | Main sketch — owns globals, `setup()` / `loop()`, peripheral init |
| `HardwareDefs.h` | Pin mappings, constants, `extern` declarations for shared hardware objects |
| `FactoryReset.h` / `.cpp` | Hardware factory-reset button logic (hold D2 on boot) |
| `TurnoutPersistence.h` / `.cpp` | NVS save / restore of turnout positions, config-version mismatch detection |
| `config.h` | CDI layout, SNIP identification, `CANONICAL_VERSION` |
| `TurnoutConfig.h` | Per-turnout CDI group (event IDs, servo endpoints, frog inversion) |
| `ServoTurnout.h` | `ServoTurnout` class — ties a `ServoGPIOBit` + `BitEventPC` to the CDI config, handles live reconfiguration |
| `ServoGPIOBit.h` | `ServoGPIOBit` — `BitEventInterface` implementation that drives one servo + frog relay pair |
| `MCPGpio.h` | `MCPGpio` — `Gpio` adapter for the MCP23017, drives pulsed frog-polarity relay pairs |
| `PCAPwm.h` | `PCAPwm` — `PWM` adapter for the PCA9685, with auto-disable after 1 s |
| `NODEID.h` | LCC node ID constant |

## Architecture

```
LCC Bus (CAN / TWAI)
        │
   OpenMRNLite stack
        │
   ServoTurnout  ×8     (DefaultConfigUpdateListener — reads CDI, rebuilds on config change)
    ├─ ServoGPIOBit      (BitEventInterface — set_state / get_current_state)
    │   ├─ PCAPwm        (PWM → PCA9685 servo channel)
    │   └─ MCPGpio       (Gpio → MCP23017 frog relay pair)
    └─ BitEventPC        (producer-consumer — consumes commands, produces state)
```

Each `ServoTurnout` owns a `ServoGPIOBit` and a `BitEventPC`. When an LCC event matching a turnout's configured event ID arrives, the `BitEventPC` consumer side calls `ServoGPIOBit::set_state()`, which:

1. Updates the internal state boolean
2. Sets the servo PWM to the configured endpoint
3. Pulses the appropriate MCP23017 frog relay pin
4. Invokes the NVS persistence callback
5. (Via `BitEventPC`) produces an `EventReport` on the LCC bus so other nodes see the new state

## Event Handling

Each turnout has **two configured LCC event IDs** (set via JMRI or any CDI tool):

| Event | CDI field | Action |
|-------|-----------|--------|
| Normal | `event_rotate_min` | Moves servo to normal endpoint, switches frog |
| Reversed | `event_rotate_max` | Moves servo to reversed endpoint, switches frog |

The same event IDs serve as both **command** (consumer) and **state** (producer):

- **Consuming:** When the node receives either event, it moves the turnout accordingly.
- **Producing:** After moving, the node produces the corresponding event to confirm the state change. Other nodes (e.g. a touchscreen panel, JMRI) see this as a state report.
- **Identify responses:** The node responds to `Identify Producer` / `Identify Consumer` / `Identify Events` with the correct `Valid`/`Invalid` status based on the current turnout position.

### Startup Behavior

On boot the node does **not** proactively produce `EventReport` messages. Instead:

1. Persisted turnout positions are read from NVS and queued as "pending restores."
2. When `apply_configuration()` runs for the initial load (triggered by `openmrn.begin()`), it computes the real servo tick values from the CDI config and *then* physically restores each turnout to its saved position.
3. Once the OpenMRN stack is running, the node responds correctly to **Identify Events** queries (which most LCC nodes and JMRI send on their own startup).

This means a control panel or touchscreen that sends Identify Events Global will immediately learn each turnout's current position.

## State Persistence (NVS)

Turnout positions are saved to ESP32 NVS (Non-Volatile Storage) under the namespace `"turnout"` with keys `t0` through `t7`. Every time a turnout is commanded, the new position is written to NVS. On boot, any previously-saved positions are restored before the LCC stack starts.

The separate `restore_state()` method actuates the hardware without triggering the persistence callback, avoiding a redundant NVS write of the value that was just read.

## Factory Reset

There are two ways the node's configuration can be reset to defaults:

### Hardware button (D2)

Hold the factory-reset button on **D2** (active LOW, external pull-up) while powering on the board. If the button is held continuously for **5 seconds**, the built-in LED flashes 5 times over 5 seconds to confirm, then:

1. SPIFFS is formatted (erasing the CDI configuration file).
2. The NVS `"turnout"` namespace is cleared (erasing saved turnout positions).
3. The board reboots.

On the subsequent boot, a fresh default configuration is created automatically.

If the button is released before 5 seconds, the reset is cancelled and normal boot continues.

### Automatic (config version mismatch)

When the firmware is updated and `CANONICAL_VERSION` in `config.h` has been incremented, the node detects that the stored config version doesn't match and treats it as a factory reset. The CDI config file is recreated by the OpenMRN stack and the NVS turnout positions are cleared, since the old saved state may not be compatible with the new configuration layout.

## CDI Configuration Fields (per turnout)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| Description | `StringConfigEntry<16>` | `""` | User-friendly name |
| Normal Event | `EventConfigEntry` | — | Event ID to command / report normal position |
| Reversed Event | `EventConfigEntry` | — | Event ID to command / report reversed position |
| Servo Normal Stop Point % | `Int16ConfigEntry` | `0` | Servo endpoint for normal (range −99 to 200) |
| Servo Reversed Stop Point % | `Int16ConfigEntry` | `100` | Servo endpoint for reversed (range −99 to 200) |
| Invert Frog Polarity | `Uint8ConfigEntry` | `0` | `0` = normal, `1` = swap frog relay pins |

## Node Identification (SNIP)

| Field | Value |
|-------|-------|
| Manufacturer | OpenMRN |
| Model | LCC Turnout Controller |
| Hardware version | *(board variant macro)* |
| Software version | 1.0.1 |

## License

See [LICENSE](LICENSE) (BSD 2-Clause).
