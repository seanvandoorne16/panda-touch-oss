#include "screen_wifi.hpp"
#include "screen_home.hpp"
#include "wifi_manager.hpp"
#include "config_manager.hpp"
#include "ui_manager.hpp"
#include "esp_log.h"
#include <string>
#include <vector>

static const char* TAG = "screen_wifi";

static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_ssid_dd     = nullptr;
static lv_obj_t* s_pass_ta     = nullptr;
static lv_obj_t* s_connect_btn = nullptr;
static lv_obj_t* s_status_lbl  = nullptr;
static lv_obj_t* s_spinner     = nullptr;

static std::vector<std::string> s_ssids;

static void update_status(const char* msg, lv_color_t color) {
    UiManager::instance().lock();
    lv_label_set_text(s_status_lbl, msg);
    lv_obj_set_style_text_color(s_status_lbl, color, 0);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);
    UiManager::instance().unlock();
}

static void on_wifi_state(WifiState state, const std::string& ip) {
    switch (state) {
        case WifiState::CONNECTED:
            update_status(("Connected: " + ip).c_str(), lv_palette_main(LV_PALETTE_GREEN));
            // Small delay then go to home screen
            lv_timer_create([](lv_timer_t*) {
                ScreenWifi::destroy();
                ScreenHome::create();
            }, 800, nullptr);
            break;
        case WifiState::FAILED:
            update_status("Connection failed. Check password.", lv_palette_main(LV_PALETTE_RED));
            break;
        case WifiState::CONNECTING:
            break;
        default:
            break;
    }
}

static void on_connect_clicked(lv_event_t* e) {
    if (s_ssids.empty()) {
        update_status("No networks found. Tap Scan.", lv_palette_main(LV_PALETTE_ORANGE));
        return;
    }

    uint16_t sel = lv_dropdown_get_selected(s_ssid_dd);
    std::string ssid = (sel < s_ssids.size()) ? s_ssids[sel] : "";
    std::string pass = lv_textarea_get_text(s_pass_ta);

    if (ssid.empty()) return;

    lv_label_set_text(s_status_lbl, "Connecting...");
    lv_obj_add_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    WifiManager::instance().on_state_change(on_wifi_state);

    ConfigManager::instance().save_wifi({ssid, pass});
    WifiManager::instance().connect(ssid, pass);
}

static void on_scan_done(const std::vector<std::string>& ssids) {
    s_ssids = ssids;
    if (ssids.empty()) {
        update_status("No networks found.", lv_palette_main(LV_PALETTE_ORANGE));
        return;
    }

    UiManager::instance().lock();
    // Build dropdown options string "SSID1\nSSID2\n..."
    std::string opts;
    for (size_t i = 0; i < ssids.size(); i++) {
        opts += ssids[i];
        if (i + 1 < ssids.size()) opts += "\n";
    }
    lv_dropdown_set_options(s_ssid_dd, opts.c_str());
    lv_label_set_text(s_status_lbl, "");
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);
    UiManager::instance().unlock();
}

static void on_scan_clicked(lv_event_t*) {
    lv_obj_add_flag(s_connect_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_status_lbl, "Scanning...");
    WifiManager::instance().scan_async(on_scan_done);
}

void ScreenWifi::create() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), 0);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // SSID dropdown
    lv_obj_t* ssid_lbl = lv_label_create(s_screen);
    lv_label_set_text(ssid_lbl, "Network");
    lv_obj_set_style_text_color(ssid_lbl, lv_color_white(), 0);
    lv_obj_align(ssid_lbl, LV_ALIGN_TOP_LEFT, 60, 80);

    s_ssid_dd = lv_dropdown_create(s_screen);
    lv_dropdown_set_options(s_ssid_dd, "Tap Scan to find networks");
    lv_obj_set_size(s_ssid_dd, 500, 50);
    lv_obj_align(s_ssid_dd, LV_ALIGN_TOP_LEFT, 60, 110);

    // Scan button
    lv_obj_t* scan_btn = lv_btn_create(s_screen);
    lv_obj_set_size(scan_btn, 120, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_LEFT, 580, 110);
    lv_obj_add_event_cb(scan_btn, on_scan_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_lbl);

    // Password field
    lv_obj_t* pass_lbl = lv_label_create(s_screen);
    lv_label_set_text(pass_lbl, "Password");
    lv_obj_set_style_text_color(pass_lbl, lv_color_white(), 0);
    lv_obj_align(pass_lbl, LV_ALIGN_TOP_LEFT, 60, 180);

    s_pass_ta = lv_textarea_create(s_screen);
    lv_textarea_set_password_mode(s_pass_ta, true);
    lv_textarea_set_one_line(s_pass_ta, true);
    lv_textarea_set_placeholder_text(s_pass_ta, "Enter password...");
    lv_obj_set_size(s_pass_ta, 640, 50);
    lv_obj_align(s_pass_ta, LV_ALIGN_TOP_LEFT, 60, 210);

    // On-screen keyboard
    lv_obj_t* kb = lv_keyboard_create(s_screen);
    lv_keyboard_set_textarea(kb, s_pass_ta);
    lv_obj_set_size(kb, LCD_W, 200);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    // Status label
    s_status_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 275);

    // Spinner (hidden by default)
    s_spinner = lv_spinner_create(s_screen, 1000, 60);
    lv_obj_set_size(s_spinner, 40, 40);
    lv_obj_align(s_spinner, LV_ALIGN_TOP_RIGHT, -20, 275);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    // Connect button
    s_connect_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_connect_btn, 200, 55);
    lv_obj_align(s_connect_btn, LV_ALIGN_TOP_MID, 0, 270);
    lv_obj_add_event_cb(s_connect_btn, on_connect_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* conn_lbl = lv_label_create(s_connect_btn);
    lv_label_set_text(conn_lbl, LV_SYMBOL_WIFI " Connect");
    lv_obj_center(conn_lbl);

    lv_scr_load(s_screen);

    // Auto-scan on open
    on_scan_clicked(nullptr);
}

void ScreenWifi::destroy() {
    if (s_screen) {
        lv_obj_del(s_screen);
        s_screen = nullptr;
    }
}
