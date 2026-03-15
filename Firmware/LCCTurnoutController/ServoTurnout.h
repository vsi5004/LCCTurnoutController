#ifndef __SERVOTURNOUT_HXX
#define __SERVOTURNOUT_HXX

#include "freertos_drivers/common/DummyGPIO.hxx"
#include "freertos_drivers/common/PWM.hxx"
#include "os/MmapGpio.hxx"
#include "TurnoutConfig.h"
#include "ServoGPIOBit.h"
#include "MCPGpio.h"
#include <Ticker.h>
#include <memory>

namespace openlcb
{

class ServoTurnout : public DefaultConfigUpdateListener
{
public:
    ServoTurnout(Node *node, const TurnoutConfig &cfg,
        const uint32_t pwmCountPerMs, PWM *pwm, Gpio *gpio,
        uint8_t turnoutIndex, uint16_t staggerDelayMs)
        : DefaultConfigUpdateListener()
        , pwmCountPerMs_(pwmCountPerMs)
        , gpioImpl_(node, 0, 0, gpio, pwm, 0, 0)
        , pc_(&gpioImpl_)
        , cfg_(cfg)
        , frogInverted_(0xFF)
        , savedCallback_()
        , savedIndex_(0)
        , hasPendingRestore_(false)
        , pendingRestoreState_(false)
        , turnoutIndex_(turnoutIndex)
        , staggerDelayMs_(staggerDelayMs)
    {
    }

    UpdateAction apply_configuration(
        int fd, bool initial_load, BarrierNotifiable *done) OVERRIDE
    {
        AutoNotify n(done);
        Serial.printf("ServoTurnout %d: apply_configuration called (initial_load=%s)\n",
                      turnoutIndex_, initial_load ? "true" : "false");

        const EventId cfg_event_min = cfg_.event_rotate_min().read(fd);
        const EventId cfg_event_max = cfg_.event_rotate_max().read(fd);
        const int16_t cfg_servo_min_pct = cfg_.servo_min_percent().read(fd);
        const int16_t cfg_servo_max_pct = cfg_.servo_max_percent().read(fd);
        const uint8_t cfg_invert_frog = cfg_.frog_inverted().read(fd);

        // 1ms duty cycle
        const uint32_t servo_ticks_0 = pwmCountPerMs_ * 1;
        // 2ms duty cycle
        const uint32_t servo_ticks_180 = pwmCountPerMs_ * 2;

        // Use a weighted average to determine num ticks for max/min.
        const uint32_t cfg_srv_ticks_min =
            ((100 - cfg_servo_min_pct) * servo_ticks_0 +
                cfg_servo_min_pct * servo_ticks_180) /
            100;
        const uint32_t cfg_srv_ticks_max =
            ((100 - cfg_servo_max_pct) * servo_ticks_0 +
                cfg_servo_max_pct * servo_ticks_180) /
            100;
        bool frogModified = false;

        if (0xFF == frogInverted_ || cfg_invert_frog != frogInverted_)
        {
          frogInverted_ = cfg_invert_frog;

          // Cast gpio_ to MCPGpio* and update inversion
          MCPGpio* gpio = static_cast<MCPGpio*>(gpioImpl_.gpio_);
          if (gpio) {
              gpio->set_frog_inverted(frogInverted_);
              frogModified = true;
          } else {
              Serial.println("Error: gpio_ is not an MCPGpio instance!");
          }
        }

        if (frogModified ||
            cfg_event_min != gpioImpl_.event_on() ||
            cfg_event_max != gpioImpl_.event_off() ||
            cfg_srv_ticks_min != gpioImpl_.pwm_max_ticks_ ||
            cfg_srv_ticks_max != gpioImpl_.pwm_min_ticks_)
        {
            auto saved_node = gpioImpl_.node();
            auto saved_gpio = gpioImpl_.gpio_;
            auto saved_pwm = gpioImpl_.pwm_;
            const bool saved_state =
                gpioImpl_.get_current_state() == EventState::VALID;

            pc_.~BitEventPC();
            gpioImpl_.~ServoGPIOBit();

            new (&gpioImpl_) ServoGPIOBit(
                saved_node, cfg_event_min, cfg_event_max, saved_gpio, saved_pwm, cfg_srv_ticks_max, cfg_srv_ticks_min );
            // Re-attach the state callback lost during placement new.
            if (savedCallback_)
            {
                gpioImpl_.set_state_callback(savedIndex_, savedCallback_);
            }
            new (&pc_) BitEventPC(&gpioImpl_);

            if (initial_load && hasPendingRestore_)
            {
                // On first boot, restore the persisted state now that the
                // real servo tick values have been applied.
                hasPendingRestore_ = false;
                // Calculate stagger delay: turnout index * configured delay
                uint32_t delayMs = turnoutIndex_ * staggerDelayMs_;
                if (delayMs > 0)
                {
                    // Schedule delayed restoration to stagger power draw
                    restoreTicker_.once_ms(delayMs, static_restore_callback, (void*)this);
                    Serial.printf("ServoTurnout %d: scheduled restore in %dms (%s)\n",
                                  turnoutIndex_, delayMs,
                                  pendingRestoreState_ ? "NORMAL" : "REVERSED");
                }
                else
                {
                    // No delay, restore immediately
                    gpioImpl_.restore_state(pendingRestoreState_);
                    Serial.printf("ServoTurnout %d: applied pending restore (%s)\n",
                                  turnoutIndex_,
                                  pendingRestoreState_ ? "NORMAL" : "REVERSED");
                }
            }
            else if (!initial_load)
            {
                // Live config change: re-apply the current state with the
                // new endpoint / frog-inversion values so the servo moves
                // to the correct position immediately.
                gpioImpl_.restore_state(saved_state);
                Serial.printf("ServoTurnout %d: live config update applied (%s)\n",
                              turnoutIndex_,
                              saved_state ? "NORMAL" : "REVERSED");
            }

            return REINIT_NEEDED;
        }
        return UPDATED;
    }

    void factory_reset(int fd) OVERRIDE
    {
        cfg_.description().write(fd, "");
        CDI_FACTORY_RESET(cfg_.servo_min_percent);
        CDI_FACTORY_RESET(cfg_.servo_max_percent);
        CDI_FACTORY_RESET(cfg_.frog_inverted);
    }

    /// Provide access to the underlying ServoGPIOBit for state
    /// restoration at startup.
    ServoGPIOBit &gpio_impl() { return gpioImpl_; }

    /// Queue a state restore to be applied in apply_configuration()
    /// once the real servo tick values are known.
    void set_pending_restore(bool state)
    {
        hasPendingRestore_ = true;
        pendingRestoreState_ = state;
    }

    /// Set the persistence callback.  Also remembers it so it can be
    /// re-attached after apply_configuration() reconstructs gpioImpl_.
    void set_state_callback(uint8_t index, TurnoutStateCallback cb)
    {
        savedIndex_ = index;
        savedCallback_ = cb;
        gpioImpl_.set_state_callback(index, std::move(cb));
    }

private:
    /// Used to compute PWM ticks for max/min servo rotation.
    const uint32_t pwmCountPerMs_;
    /// all the rest are owned and must be reset on config change.
    ServoGPIOBit gpioImpl_;    /// has on/off events, Node*, and Gpio*
    BitEventPC pc_;            /// producer-consumer for state events
    const TurnoutConfig cfg_;
    uint8_t frogInverted_;
    TurnoutStateCallback savedCallback_; /// callback preserved across reconfig
    uint8_t savedIndex_;                 /// turnout index preserved across reconfig
    bool hasPendingRestore_;   /// true if a restore is queued for initial load
    bool pendingRestoreState_; /// the state to restore
    uint8_t turnoutIndex_;     /// index of this turnout (0-7)
    uint16_t staggerDelayMs_;  /// milliseconds delay per turnout for staggered init
    mutable Ticker restoreTicker_; /// timer for delayed state restoration

    /// Static callback for the restore Ticker.
    static void static_restore_callback(void* arg) {
        ServoTurnout* self = static_cast<ServoTurnout*>(arg);
        self->gpioImpl_.restore_state(self->pendingRestoreState_);
        Serial.printf("ServoTurnout %d: completed delayed restore (%s)\n",
                      self->turnoutIndex_,
                      self->pendingRestoreState_ ? "NORMAL" : "REVERSED");
    }
};

} // namespace openlcb

#endif // SERVOTURNOUT_HXX_