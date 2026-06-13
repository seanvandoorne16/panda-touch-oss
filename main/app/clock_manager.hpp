#pragma once
#include <cstdint>
#include <string>
#include "esp_err.h"

// Fixes issue #196: clock in status bar
// Uses SNTP to sync time; falls back to uptime counter if no internet

class ClockManager {
public:
    static ClockManager& instance();

    esp_err_t init(const char* ntp_server = "pool.ntp.org",
                   const char* timezone   = "CET-1CEST,M3.5.0,M10.5.0/3");

    // "14:35" format
    std::string time_str()     const;
    // "Sat 13 Jun" format
    std::string date_str()     const;
    bool        synced()       const { return _synced; }

private:
    ClockManager() = default;
    static void sntp_sync_cb(struct timeval* tv);
    bool _synced = false;
};
