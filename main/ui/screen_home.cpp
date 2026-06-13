#include "screen_home.hpp"
#include "screen_printer.hpp"
#include "screen_wifi.hpp"
#include "ui_manager.hpp"
#include "battery_monitor.hpp"
#include "wifi_manager.hpp"
#include "config_manager.hpp"
#include "bambu_client.hpp"
#include "esp_log.h"
#include <vector>
#include <map>
#include <memory>
#include <string>

static const char* TAG = "screen_home";

static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_bat_label   = nullptr;
static lv_obj_t* s_bat_pct     = nullptr;
static lv_obj_t* s_wifi_label  = nullptr;
static lv_obj_t* s_printer_list = nullptr;
static lv_timer_t* s_status_timer = nullptr;

// Active BambuClient connections
static std::map<std::string, std::shared_ptr<BambuClient>> s_clients;
// Card widgets per serial
static std::map<std::string, lv_obj_t*> s_cards;

static lv_color_t status_color(PrinterStatus st) {
    switch (st) {
        case PrinterStatus::PRINTING:  return lv_palette_main(LV_PALETTE_GREEN);
        case PrinterStatus::PAUSED:    return lv_palette_main(LV_PALETTE_YELLOW);
        case PrinterStatus::FAILED:    return lv_palette_main(LV_PALETTE_RED);
        case PrinterStatus::OFFLINE:   return lv_palette_lighten(LV_PALETTE_GREY, 2);
        case PrinterStatus::FINISHED:  return lv_palette_main(LV_PALETTE_BLUE);
        default:                       return lv_palette_lighten(LV_PALETTE_GREY, 3);
    }
}

static void update_status_bar(lv_timer_t*) {
    auto& batt = BatteryMonitor::instance();
    batt.update();

    UiManager::instance().lock();

    // Battery icon + percentage
    char bat_buf[32];
    if (batt.level_pct() >= 0) {
        const char* sym = batt.lv_symbol();
        snprintf(bat_buf, sizeof(bat_buf), "%s %d%%", sym, batt.level_pct());
    } else {
        snprintf(bat_buf, sizeof(bat_buf), LV_SYMBOL_BATTERY_EMPTY " --%%");
    }
    lv_label_set_text(s_bat_label, bat_buf);

    // WiFi icon + IP
    auto& wifi = WifiManager::instance();
    if (wifi.state() == WifiState::CONNECTED) {
        std::string wlabel = std::string(LV_SYMBOL_WIFI " ") + wifi.ip();
        lv_label_set_text(s_wifi_label, wlabel.c_str());
        lv_obj_set_style_text_color(s_wifi_label, lv_palette_main(LV_PALETTE_GREEN), 0);
    } else {
        lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " --");
        lv_obj_set_style_text_color(s_wifi_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
    }

    UiManager::instance().unlock();
}

static lv_obj_t* create_printer_card(lv_obj_t* parent, const PrinterConfig& cfg) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, LCD_W - 40, 110);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 12, 0);

    // Printer name
    lv_obj_t* name = lv_label_create(card);
    lv_label_set_text(name, cfg.name.c_str());
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_18, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

    // Status dot
    lv_obj_t* dot = lv_obj_create(card);
    lv_obj_set_size(dot, 12, 12);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, 0, 4);

    // Status text
    lv_obj_t* st_lbl = lv_label_create(card);
    lv_label_set_text(st_lbl, "Offline");
    lv_obj_set_style_text_color(st_lbl, lv_palette_lighten(LV_PALETTE_GREY, 2), 0);
    lv_obj_align(st_lbl, LV_ALIGN_TOP_RIGHT, -20, 2);

    // Progress bar
    lv_obj_t* bar = lv_bar_create(card);
    lv_obj_set_size(bar, LCD_W - 80, 10);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_LEFT, 0, -20);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    // Progress text + ETA
    lv_obj_t* prog_lbl = lv_label_create(card);
    lv_label_set_text(prog_lbl, "0%  |  ETA: --");
    lv_obj_set_style_text_color(prog_lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_align(prog_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Tap → printer detail screen
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        auto* serial = (const char*)lv_event_get_user_data(e);
        if (s_clients.count(serial)) {
            ScreenPrinter::show(s_clients[serial]);
        }
    }, LV_EVENT_CLICKED, (void*)cfg.serial.c_str());

    return card;
}

void ScreenHome::refresh_printer(const PrinterState& state) {
    auto it = s_cards.find(state.serial);
    if (it == s_cards.end()) return;

    lv_obj_t* card = it->second;
    UiManager::instance().lock();

    // Update status dot color
    lv_obj_t* dot    = lv_obj_get_child(card, 1);
    lv_obj_t* st_lbl = lv_obj_get_child(card, 2);
    lv_obj_t* bar    = lv_obj_get_child(card, 3);
    lv_obj_t* p_lbl  = lv_obj_get_child(card, 4);

    lv_obj_set_style_bg_color(dot, status_color(state.status), 0);
    lv_label_set_text(st_lbl, state.status_str());

    if (state.status == PrinterStatus::PRINTING || state.status == PrinterStatus::PAUSED) {
        lv_bar_set_value(bar, state.progress_pct, LV_ANIM_ON);

        char buf[64];
        snprintf(buf, sizeof(buf), "%d%%  |  ETA: %s",
                 state.progress_pct, state.remaining_str().c_str());
        lv_label_set_text(p_lbl, buf);
    } else {
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
        lv_label_set_text(p_lbl, state.gcode_file.empty() ? "" : state.gcode_file.c_str());
    }

    UiManager::instance().unlock();
}

void ScreenHome::create() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d0d0d), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ── Status bar ────────────────────────────────────────────────────────────
    lv_obj_t* bar = lv_obj_create(s_screen);
    lv_obj_set_size(bar, LCD_W, 36);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t* title = lv_label_create(bar);
    lv_label_set_text(title, "Panda Touch OSS");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    // Battery indicator (top-right) — fixes issue #189
    s_bat_label = lv_label_create(bar);
    lv_label_set_text(s_bat_label, LV_SYMBOL_BATTERY_FULL " --%%");
    lv_obj_set_style_text_color(s_bat_label, lv_color_white(), 0);
    lv_obj_align(s_bat_label, LV_ALIGN_RIGHT_MID, -12, 0);

    // WiFi indicator
    s_wifi_label = lv_label_create(bar);
    lv_label_set_text(s_wifi_label, LV_SYMBOL_WIFI " --");
    lv_obj_set_style_text_color(s_wifi_label, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_align_to(s_wifi_label, s_bat_label, LV_ALIGN_OUT_LEFT_MID, -16, 0);

    // ── Settings button ───────────────────────────────────────────────────────
    lv_obj_t* settings_btn = lv_btn_create(s_screen);
    lv_obj_set_size(settings_btn, 120, 36);
    lv_obj_align(settings_btn, LV_ALIGN_TOP_RIGHT, -12, 40);
    lv_obj_set_style_bg_color(settings_btn, lv_color_hex(0x0f3460), 0);
    lv_obj_add_event_cb(settings_btn, [](lv_event_t*) {
        // TODO: open settings screen
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* s_lbl = lv_label_create(settings_btn);
    lv_label_set_text(s_lbl, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_center(s_lbl);

    // ── Add printer button ────────────────────────────────────────────────────
    lv_obj_t* add_btn = lv_btn_create(s_screen);
    lv_obj_set_size(add_btn, 140, 36);
    lv_obj_align(add_btn, LV_ALIGN_TOP_LEFT, 12, 40);
    lv_obj_set_style_bg_color(add_btn, lv_color_hex(0x533483), 0);
    lv_obj_add_event_cb(add_btn, [](lv_event_t*) {
        // TODO: add printer wizard
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* a_lbl = lv_label_create(add_btn);
    lv_label_set_text(a_lbl, LV_SYMBOL_PLUS " Add Printer");
    lv_obj_center(a_lbl);

    // ── Printer list (scrollable) ─────────────────────────────────────────────
    s_printer_list = lv_obj_create(s_screen);
    lv_obj_set_size(s_printer_list, LCD_W, LCD_H - 88);
    lv_obj_align(s_printer_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_printer_list, lv_color_hex(0x0d0d0d), 0);
    lv_obj_set_style_border_width(s_printer_list, 0, 0);
    lv_obj_set_flex_flow(s_printer_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_printer_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(s_printer_list, 10, 0);
    lv_obj_set_style_pad_all(s_printer_list, 10, 0);

    // ── Load & connect printers ───────────────────────────────────────────────
    auto printers = ConfigManager::instance().get_printers();
    if (printers.empty()) {
        lv_obj_t* no_p = lv_label_create(s_printer_list);
        lv_label_set_text(no_p, "No printers configured.\nTap \"Add Printer\" to get started.");
        lv_obj_set_style_text_color(no_p, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_set_style_text_align(no_p, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(no_p);
    } else {
        for (const auto& cfg : printers) {
            lv_obj_t* card = create_printer_card(s_printer_list, cfg);
            s_cards[cfg.serial] = card;

            // Connect MQTT
            auto client = std::make_shared<BambuClient>(cfg.serial, cfg.ip, cfg.access_code);
            client->on_update([](const PrinterState& st) {
                UiManager::instance().lock();
                ScreenHome::refresh_printer(st);
                UiManager::instance().unlock();
            });
            client->connect();
            s_clients[cfg.serial] = client;
        }
    }

    lv_scr_load(s_screen);

    // Status bar refresh every 10 s
    s_status_timer = lv_timer_create(update_status_bar, 10000, nullptr);
    update_status_bar(nullptr);
}

void ScreenHome::destroy() {
    if (s_status_timer) { lv_timer_del(s_status_timer); s_status_timer = nullptr; }
    for (auto& [serial, client] : s_clients) client->disconnect();
    s_clients.clear();
    s_cards.clear();
    if (s_screen) { lv_obj_del(s_screen); s_screen = nullptr; }
}
