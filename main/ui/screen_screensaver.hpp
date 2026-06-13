#pragma once
#include "printer_state.hpp"

// Fixes issue #110: screensaver showing print status while display is "idle"
// Shows minimal status overlay on black background.
// Touch wakes to full UI.
namespace ScreenSaver {
    void show(const PrinterState& state);
    void update(const PrinterState& state);
    void dismiss();
    bool active();
}
