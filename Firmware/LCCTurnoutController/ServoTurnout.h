#ifndef __SERVOTURNOUT_HXX
#define __SERVOTURNOUT_HXX

#include "freertos_drivers/common/DummyGPIO.hxx"
#include "freertos_drivers/common/PWM.hxx"
#include "os/MmapGpio.hxx"
#include "TurnoutConfig.h"
#include "ServoGPIOBit.h"
#include "MCPGpio.h"
#include <memory>

namespace openlcb
{

class ServoTurnout : public DefaultConfigUpdateListener
{
public:
    ServoTurnout(Node *node, const TurnoutConfig &cfg,
        const uint32_t pwmCountPerMs, PWM *pwm, Gpio *gpio)
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
    {
    }

    UpdateAction apply_configuration(
        int fd, bool initial_load, BarrierNotifiable *done) OVERRIDE
    {
        AutoNotify n(done);

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
            cfg_event_min != gpioImpl_.event_off() ||
            cfg_event_max != gpioImpl_.event_on() ||
            cfg_srv_ticks_min != gpioImpl_.pwm_min_ticks_ ||
            cfg_srv_ticks_max != gpioImpl_.pwm_max_ticks_)
        {
            auto saved_node = gpioImpl_.node();
            auto saved_gpio = gpioImpl_.gpio_;
            auto saved_pwm = gpioImpl_.pwm_;

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

            // On first boot, restore the persisted state now that the real
            // servo tick values have been applied.
            if (initial_load && hasPendingRestore_)
            {
                hasPendingRestore_ = false;
                gpioImpl_.restore_state(pendingRestoreState_);
                Serial.printf("ServoTurnout: applied pending restore (%s)\n",
                              pendingRestoreState_ ? "NORMAL" : "REVERSED");
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
};

} // namespace openlcb

#endif // SERVOTURNOUT_HXX_