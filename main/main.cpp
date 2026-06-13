#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "config_manager.hpp"
#include "wifi_manager.hpp"
#include "battery_monitor.hpp"
#include "ui_manager.hpp"
#include "screen_wifi.hpp"
#include "screen_home.hpp"

static const char* TAG = "main";

// LVGL tick task — runs every 5 ms
static void lvgl_task(void*) {
    for (;;) {
        lv_tick_inc(5);
        UiManager::instance().tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Panda Touch OSS starting");

    // 1. NVS / config
    ESP_ERROR_CHECK(ConfigManager::instance().init());

    // 2. Display + touch
    ESP_ERROR_CHECK(UiManager::instance().init());
    uint8_t brt = ConfigManager::instance().get_brightness();
    UiManager::instance().set_brightness(brt);

    // 3. Battery monitor
    ESP_ERROR_CHECK(BatteryMonitor::instance().init(
        pins::BAT_ADC, pins::BAT_CHG));

    // 4. WiFi
    ESP_ERROR_CHECK(WifiManager::instance().init());

    // 5. LVGL task (core 1 to avoid contention with WiFi on core 0)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, nullptr, 5, nullptr, 1);

    // 6. Decide first screen
    WifiConfig wifi = ConfigManager::instance().get_wifi();
    if (wifi.ssid.empty()) {
        // First boot — show WiFi setup
        UiManager::instance().lock();
        ScreenWifi::create();
        UiManager::instance().unlock();
    } else {
        // Try to connect to saved network, show home immediately
        WifiManager::instance().connect(wifi.ssid, wifi.password);
        WifiManager::instance().on_state_change([](WifiState s, const std::string& ip) {
            if (s == WifiState::FAILED) {
                ESP_LOGW(TAG, "WiFi failed, showing setup screen");
                UiManager::instance().lock();
                ScreenWifi::create();
                UiManager::instance().unlock();
            }
        });

        UiManager::instance().lock();
        ScreenHome::create();
        UiManager::instance().unlock();
    }

    // Main loop — nothing to do here; LVGL runs in its own task
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
