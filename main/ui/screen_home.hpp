#pragma once
#include "lvgl.h"
#include "printer_state.hpp"

namespace ScreenHome {
    void create();
    void destroy();
    // Called when a printer state updates
    void refresh_printer(const PrinterState& state);
}
