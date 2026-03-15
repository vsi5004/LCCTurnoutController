// ── Arduino macro overrides (must precede library includes) ─────────
#include <Arduino.h>
#include <Ticker.h>

#undef HIGH
#undef LOW
#undef OUTPUT

#define ARDUINO_HIGH 1
#define ARDUINO_LOW 0
#define ARDUINO_OUTPUT 3
#define ARDUINO_INPUT 1

// ── Library headers ─────────────────────────────────────────────────
#include <SPIFFS.h>
#include <Preferences.h>
#include <OpenMRNLite.h>
#include "freertos_drivers/esp32/Esp32Can.hxx"
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Adafruit_MCP23X17.h>

// ── Project headers ─────────────────────────────────────────────────
#include "HardwareDefs.h"
#include "NODEID.h"
#include "MCPGpio.h"
#include "ServoTurnout.h"
#include "PCAPwm.h"
#include "FactoryReset.h"
#include "TurnoutPersistence.h"

// ── CDI / OpenMRN statics ───────────────────────────────────────────
static constexpr openlcb::ConfigDef cfg(0);

namespace openlcb {
const char CDI_FILENAME[]              = "/spiffs/cdi.xml";
extern const char CDI_DATA[]           = "";
extern const char *const CONFIG_FILENAME     = "/spiffs/openlcb_config";
extern const size_t CONFIG_FILE_SIZE         = cfg.seg().size() + cfg.seg().offset();
extern const char *const SNIP_DYNAMIC_FILENAME = CONFIG_FILENAME;
}

// ── Global hardware objects (declared extern in HardwareDefs.h) ─────
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
Adafruit_MCP23X17       mcp;
Esp32Can                can0(GPIO_NUM_8, GPIO_NUM_7);  // TX=GPIO8(D9/PA5), RX=GPIO7(D8/PA7)
OpenMRN                 openmrn(NODE_ID);
Preferences             turnoutPrefs;

PCAPwm              *servoPwmWrappers[NUM_TURNOUTS];
MCPGpio             *gpioWrappers[NUM_TURNOUTS];
openlcb::ServoTurnout *servoTurnouts[NUM_TURNOUTS];

// ── Debug: config space write logger ────────────────────────────────
// Wraps the real config FileMemorySpace to log every CDI write that
// arrives over LCC.  Lets us see whether JMRI is writing config at all
// and whether UPDATE_COMPLETE (which triggers apply_configuration) follows.
class LoggingConfigSpace : public MemorySpace {
public:
    LoggingConfigSpace(MemorySpace *delegate) : delegate_(delegate) {}
    bool read_only() override { return delegate_->read_only(); }
    address_t max_address() override { return delegate_->max_address(); }
    size_t read(address_t src, uint8_t *dst, size_t len,
               errorcode_t *err, Notifiable *again) override {
        return delegate_->read(src, dst, len, err, again);
    }
    size_t write(address_t dest, const uint8_t *data, size_t len,
                 errorcode_t *err, Notifiable *again) override {
        Serial.printf("DEBUG ConfigSpace WRITE: offset=%u len=%u\n",
                      (unsigned)dest, (unsigned)len);
        return delegate_->write(dest, data, len, err, again);
    }
private:
    MemorySpace *delegate_;
};

// ── MCP23017 frog-relay init ────────────────────────────────────────
static void setup_frog_gpio() {
    if (!mcp.begin_I2C()) {
        Serial.println("Failed to initialize MCP23017!");
        while (1);
    }
    for (uint8_t i = 0; i < NUM_TURNOUTS; i++) {
        mcp.pinMode(TURNOUT_FROG_PINS[i][0], ARDUINO_OUTPUT);
        mcp.pinMode(TURNOUT_FROG_PINS[i][1], ARDUINO_OUTPUT);
        mcp.digitalWrite(TURNOUT_FROG_PINS[i][0], ARDUINO_LOW);
        mcp.digitalWrite(TURNOUT_FROG_PINS[i][1], ARDUINO_LOW);
    }
}

// ═══════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);

    // Init LED early so the factory-reset flash sequence can use it.
    pinMode(LED_BUILTIN, ARDUINO_OUTPUT);
    digitalWrite(LED_BUILTIN, ARDUINO_LOW);

    // ── Disable PWM outputs immediately to prevent servo brownout ───
    // Pull nPWMENABLE HIGH to disable PCA9685 outputs during init
    pinMode(PCA9685_nOE_PIN, ARDUINO_OUTPUT);
    digitalWrite(PCA9685_nOE_PIN, ARDUINO_HIGH);
    Serial.println("PWM outputs disabled via nOE pin");

    Serial.println("\n\n=== LCC Turnout Controller ===");
    Serial.printf("Node ID: 0x%012llX\n", NODE_ID);

    // ── Hardware factory-reset button ───────────────────────────────
    check_factory_reset_button();

    // ── SPIFFS ──────────────────────────────────────────────────────
    if (!SPIFFS.begin()) {
        Serial.println("SPIFFS failed to mount, formatting...");
        if (!SPIFFS.begin(true)) {
            Serial.println("SPIFFS mount failed. Halting.");
            while (1);
        }
    }

    // ── CDI config ──────────────────────────────────────────────────
    openmrn.create_config_descriptor_xml(cfg, openlcb::CDI_FILENAME);
    bool needsFactoryReset = detect_config_version_mismatch();
    openmrn.stack()->create_config_file_if_needed(
        cfg.seg().internal_config(),
        openlcb::CANONICAL_VERSION,
        openlcb::CONFIG_FILE_SIZE);

    // ── Peripherals ─────────────────────────────────────────────────
    setup_frog_gpio();
    pwm.begin();
    
    // Clear all PWM channel registers before enabling outputs
    for (uint8_t i = 0; i < 16; i++) {
        pwm.setPWM(i, 0, 0);  // Set all channels to off
    }
    
    pwm.setOscillatorFrequency(27000000);
    pwm.setPWMFreq(SERVO_FREQ);
    
    // Now it's safe to enable PWM outputs - channels are all cleared
    digitalWrite(PCA9685_nOE_PIN, ARDUINO_LOW);
    Serial.println("PWM outputs enabled (all channels initialized to off)");

    // ── Read global configuration ───────────────────────────────
    uint16_t staggerDelayMs = 300; // default if config can't be read
    int fd = ::open(openlcb::CONFIG_FILENAME, O_RDONLY);
    if (fd >= 0) {
        staggerDelayMs = cfg.seg().global().servo_stagger_delay_ms().read(fd);
        ::close(fd);
        Serial.printf("Servo stagger delay: %dms\n", staggerDelayMs);
    } else {
        Serial.println("Could not read config, using default stagger delay");
    }

    // ── Create turnout objects ──────────────────────────────────
    for (uint8_t i = 0; i < NUM_TURNOUTS; i++) {
        gpioWrappers[i]    = new MCPGpio(&mcp, TURNOUT_FROG_PINS[i][0],
                                         TURNOUT_FROG_PINS[i][1], false);
        servoPwmWrappers[i] = new PCAPwm(pwm, i);
        servoTurnouts[i]    = new openlcb::ServoTurnout(
            openmrn.stack()->node(),
            cfg.seg().turnouts().entry(i),
            PWM_COUNT_PER_MS, servoPwmWrappers[i], gpioWrappers[i],
            i, staggerDelayMs);
        // Note: ServoTurnout auto-registers with ConfigUpdateService in constructor
    }

    // ── NVS persistence & state restore ─────────────────────────────
    restore_turnout_states(needsFactoryReset);

    // ── Start OpenMRN / CAN ─────────────────────────────────────────
    can0.begin();
    openmrn.add_can_port(&can0);
    openmrn.begin();

    // ── Install config-write logger ─────────────────────────────────
    // After begin(), the stack has registered a FileMemorySpace for
    // SPACE_CONFIG. Swap it with our logging wrapper so we can see
    // every CDI write that arrives from JMRI.
    {
        auto *reg = openmrn.stack()->memory_config_handler()->registry();
        auto *node = openmrn.stack()->node();
        auto *origSpace = reg->lookup(node, openlcb::MemoryConfigDefs::SPACE_CONFIG);
        if (origSpace) {
            auto *logged = new LoggingConfigSpace(origSpace);
            reg->erase(node, openlcb::MemoryConfigDefs::SPACE_CONFIG, origSpace);
            reg->insert(node, openlcb::MemoryConfigDefs::SPACE_CONFIG, logged);
            Serial.println("Config space write logging installed");
        } else {
            Serial.println("WARNING: could not find SPACE_CONFIG to wrap");
        }
    }

    openmrn.start_executor_thread();

    Serial.println("=== Initialization Complete ===\n");
}

// ═══════════════════════════════════════════════════════════════════
void loop() {
    openmrn.loop();

    // Heartbeat LED
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink >= 1000) {
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        lastBlink = millis();
    }
}
