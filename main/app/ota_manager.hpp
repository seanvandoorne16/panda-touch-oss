#pragma once
#include <functional>
#include "esp_err.h"

using OtaProgressCallback = std::function<void(int pct)>;
using OtaDoneCallback     = std::function<void(bool success, const char* message)>;

class OtaManager {
public:
    static OtaManager& instance();

    // Firmware version baked in at build time
    static const char* current_version();

    // Returns true while a download/flash is running
    bool is_updating() const { return _updating; }

    // Download firmware from url and flash it.
    // progress_cb: called with 0-100 during download (from OTA task, needs LVGL lock)
    // done_cb:     called with success=true + reboot, or false + error message
    void start_update(const char* url,
                      OtaProgressCallback progress_cb,
                      OtaDoneCallback     done_cb);

private:
    OtaManager() = default;

    struct UpdateArgs {
        const char*         url;
        OtaProgressCallback progress_cb;
        OtaDoneCallback     done_cb;
        OtaManager*         self;
    };

    static void ota_task(void* arg);

    bool _updating = false;
};
