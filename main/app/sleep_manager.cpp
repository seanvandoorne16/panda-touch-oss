#include "sleep_manager.hpp"
#include "ui_manager.hpp"
#include "battery_monitor.hpp"
#include "esp_log.h"

static const char* TAG = "sleep";

SleepManager& SleepManager::instance() {
    static SleepManager inst;
    return inst;
}

esp_err_t SleepManager::init() {
    return ESP_OK;
}

void SleepManager::touch_activity() {
    _idle_seconds = 0;
    if (_sleeping) wake_up();
}

void SleepManager::tick() {
    if (_sleeping) return;

    _idle_seconds++;

    uint16_t timeout = _printing ? _print_timeout : _idle_timeout;
    if (timeout > 0 && _idle_seconds >= timeout) {
        go_sleep();
    }
}

void SleepManager::go_sleep() {
    if (_sleeping) return;
    _sleeping = true;
    ESP_LOGI(TAG, "Screen dimmed (backlight off)");
    // Dim to 1% not 0% — keeps charge circuit visible LED behaviour
    UiManager::instance().set_brightness(1);
}

void SleepManager::wake_up() {
    _sleeping     = false;
    _idle_seconds = 0;
    ESP_LOGI(TAG, "Screen wake");
    // Restore saved brightness
    extern uint8_t g_brightness;
    UiManager::instance().set_brightness(g_brightness);
}
