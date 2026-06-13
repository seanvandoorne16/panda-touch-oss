#pragma once
#include "lvgl.h"

// WiFi setup screen — shown when no SSID is saved or connection fails
// Fixes: screen freeze on WiFi not found (#365), WPA2/WPA3 support (#331)
namespace ScreenWifi {
    void create();   // push onto LVGL screen stack
    void destroy();
}
