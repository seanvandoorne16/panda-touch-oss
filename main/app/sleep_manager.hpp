#pragma once
#include <cstdint>
#include "esp_err.h"

// Fixes issues #228, #260, #328: crash on wake from sleep.
// Root cause in official firmware: full deep-sleep + display re-init race condition.
// Fix: never use deep sleep — only dim backlight to 0%.
// WiFi and MQTT stay alive, so wake is instant with no reboot.

class SleepManager {
public:
    static SleepManager& instance();

    esp_err_t init();

    // Called from touch ISR or LVGL input handler — resets idle timer
    void touch_activity();

    // Call from main/LVGL task every second
    void tick();

    // Whether the screen is currently dimmed
    bool is_sleeping() const { return _sleeping; }

    // Separate timeouts: 0 = never sleep
    void set_idle_timeout_sec(uint16_t s)    { _idle_timeout    = s; }
    void set_print_timeout_sec(uint16_t s)   { _print_timeout   = s; }

    // Must be updated so we know if a print is active
    void set_printing(bool v) { _printing = v; }

private:
    SleepManager() = default;
    void go_sleep();
    void wake_up();

    uint16_t _idle_timeout   = 300;   // 5 min default
    uint16_t _print_timeout  = 0;     // never during print (issue #59)
    uint32_t _idle_seconds   = 0;
    bool     _sleeping       = false;
    bool     _printing       = false;
};
