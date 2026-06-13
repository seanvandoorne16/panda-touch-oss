#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config_manager.hpp"
#include "wifi_manager.hpp"
#include "battery_monitor.hpp"
#include "sleep_manager.hpp"
#include "clock_manager.hpp"
#include "ui_manager.hpp"
#include "screen_wifi.hpp"
#include "screen_home.hpp"

static const char* TAG = "main";

// FIX M4: brightness lives here, not in a UI file
uint8_t g_brightness = 80;

// LVGL tick + sleep tick task — runs every 5 ms on core 1
static void lvgl_task(void*) {
    uint32_t sleep_counter = 0;
    for (;;) {
        lv_tick_inc(5);
        UiManager::instance().tick();

        // Tick sleep manager every 1 second
        if (++sleep_counter >= 200) {
            sleep_counter = 0;
            SleepManager::instance().tick();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Panda Touch OSS starting");

    // 1. NVS / config
    ESP_ERROR_CHECK(ConfigManager::instance().init());

    // 2. Display + touch
    ESP_ERROR_CHECK(UiManager::instance().init());
    g_brightness = ConfigManager::instance().get_brightness();
    UiManager::instance().set_brightness(g_brightness);

    // 3. Battery monitor
    ESP_ERROR_CHECK(BatteryMonitor::instance().init(
        pins::BAT_ADC, pins::BAT_CHG));

    // 4. Sleep manager (fixes crash-on-wake #228/#260/#328)
    //    — uses backlight dim only, never deep sleep
    ESP_ERROR_CHECK(SleepManager::instance().init());
    SleepManager::instance().set_idle_timeout_sec(
        ConfigManager::instance().get_sleep_sec());
    SleepManager::instance().set_print_timeout_sec(0); // never during print

    // 5. WiFi
    ESP_ERROR_CHECK(WifiManager::instance().init());

    // 6. FIX H3: use add_state_listener (multi-observer) instead of on_state_change
    WifiManager::instance().add_state_listener([](WifiState s, const std::string&) {
        if (s == WifiState::CONNECTED) {
            ClockManager::instance().init();  // SNTP after WiFi up (#196)
        } else if (s == WifiState::FAILED) {
            ESP_LOGW(TAG, "WiFi failed, showing setup screen");
            UiManager::instance().lock();
            ScreenWifi::create();
            UiManager::instance().unlock();
        }
    });

    // 7. LVGL task (core 1)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 5, nullptr, 1);

    // 8. Decide first screen
    WifiConfig wifi = ConfigManager::instance().get_wifi();
    if (wifi.ssid.empty()) {
        UiManager::instance().lock();
        ScreenWifi::create();
        UiManager::instance().unlock();
    } else {
        WifiManager::instance().connect(wifi.ssid, wifi.password);
        UiManager::instance().lock();
        ScreenHome::create();
        UiManager::instance().unlock();
    }

    // Main loop
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
