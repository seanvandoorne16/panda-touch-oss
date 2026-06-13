#include "screen_settings.hpp"
#include "screen_home.hpp"
#include "screen_ota.hpp"
#include "ui_manager.hpp"
#include "config_manager.hpp"
#include "sleep_manager.hpp"
#include "clock_manager.hpp"
#include "ota_manager.hpp"
#include "esp_log.h"
#include <cstdio>

static const char* TAG = "settings";

static lv_obj_t* s_screen = nullptr;

// FIX M4: g_brightness defined in main.cpp, not here
extern uint8_t g_brightness;

static lv_obj_t* section_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_palette_lighten(LV_PALETTE_BLUE, 1), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    return lbl;
}

static lv_obj_t* row_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    return lbl;
}

void ScreenSettings::show() {
    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d0d0d), 0);

    // Back
    lv_obj_t* back = lv_btn_create(s_screen);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, [](lv_event_t*) {
        ScreenSettings::destroy();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(bl);

    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    // Scrollable content
    lv_obj_t* cont = lv_obj_create(s_screen);
    lv_obj_set_size(cont, LCD_W - 40, LCD_H - 60);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(cont, 16, 0);
    lv_obj_set_style_pad_all(cont, 16, 0);

    // ── Display ───────────────────────────────────────────────────────────────
    section_label(cont, "Display");

    // Brightness 1-100% (fixes issue #273 — allows below 10%)
    lv_obj_t* brt_row = lv_obj_create(cont);
    lv_obj_set_size(brt_row, LCD_W - 72, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(brt_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(brt_row, 0, 0);
    lv_obj_set_flex_flow(brt_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(brt_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    row_label(brt_row, "Brightness");

    lv_obj_t* brt_val = lv_label_create(brt_row);
    lv_obj_set_style_text_color(brt_val, lv_color_white(), 0);

    lv_obj_t* brt_slider = lv_slider_create(cont);
    lv_obj_set_size(brt_slider, LCD_W - 100, 24);
    lv_slider_set_range(brt_slider, 1, 100);   // 1% minimum (fixes #273)
    lv_slider_set_value(brt_slider, g_brightness, LV_ANIM_OFF);

    char brt_buf[8]; snprintf(brt_buf, sizeof(brt_buf), "%d%%", g_brightness);
    lv_label_set_text(brt_val, brt_buf);

    lv_obj_add_event_cb(brt_slider, [](lv_event_t* e) {
        lv_obj_t* slider = lv_event_get_target(e);
        lv_obj_t* val_lbl = (lv_obj_t*)lv_event_get_user_data(e);
        int v = lv_slider_get_value(slider);
        char buf[8]; snprintf(buf, sizeof(buf), "%d%%", v);
        lv_label_set_text(val_lbl, buf);
        g_brightness = (uint8_t)v;
        UiManager::instance().set_brightness(g_brightness);
        ConfigManager::instance().save_brightness(g_brightness);
    }, LV_EVENT_VALUE_CHANGED, brt_val);

    // ── Sleep ─────────────────────────────────────────────────────────────────
    section_label(cont, "Sleep (fixes #59, #268)");

    // Idle sleep timeout
    row_label(cont, "Idle screen off after");
    static const char* timeout_opts = "Never\n1 min\n2 min\n5 min\n10 min\n30 min";
    static const uint16_t timeout_vals[] = {0, 60, 120, 300, 600, 1800};

    lv_obj_t* idle_dd = lv_dropdown_create(cont);
    lv_dropdown_set_options(idle_dd, timeout_opts);
    lv_obj_set_width(idle_dd, 300);
    uint16_t cur_idle = ConfigManager::instance().get_sleep_sec();
    for (int i = 0; i < 6; i++) {
        if (timeout_vals[i] == cur_idle) { lv_dropdown_set_selected(idle_dd, i); break; }
    }
    lv_obj_add_event_cb(idle_dd, [](lv_event_t* e) {
        lv_obj_t* dd = lv_event_get_target(e);
        uint16_t v = timeout_vals[lv_dropdown_get_selected(dd)];
        SleepManager::instance().set_idle_timeout_sec(v);
        ConfigManager::instance().save_sleep_sec(v);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Print sleep timeout (separate — fixes issue #268)
    row_label(cont, "Screen off during print");
    lv_obj_t* print_dd = lv_dropdown_create(cont);
    lv_dropdown_set_options(print_dd, timeout_opts);
    lv_obj_set_width(print_dd, 300);
    lv_dropdown_set_selected(print_dd, 0); // default: never during print
    lv_obj_add_event_cb(print_dd, [](lv_event_t* e) {
        lv_obj_t* dd = lv_event_get_target(e);
        uint16_t v = timeout_vals[lv_dropdown_get_selected(dd)];
        SleepManager::instance().set_print_timeout_sec(v);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── Clock ─────────────────────────────────────────────────────────────────
    section_label(cont, "Clock (fixes #196)");

    lv_obj_t* tz_row = lv_obj_create(cont);
    lv_obj_set_size(tz_row, LCD_W - 72, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(tz_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tz_row, 0, 0);
    lv_obj_set_flex_flow(tz_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tz_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    row_label(tz_row, "Timezone");

    static const char* tz_opts =
        "UTC\nCET (Europe)\nEET (E.Europe)\nGMT (UK)\n"
        "EST (US East)\nCST (US Central)\nMST (US Mountain)\nPST (US West)\n"
        "JST (Japan)\nAEST (Australia)";
    static const char* tz_vals[] = {
        "UTC0",
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "EET-2EEST,M3.5.0/3,M10.5.0/4",
        "GMT0BST,M3.5.0/1,M10.5.0",
        "EST5EDT,M3.2.0,M11.1.0",
        "CST6CDT,M3.2.0,M11.1.0",
        "MST7MDT,M3.2.0,M11.1.0",
        "PST8PDT,M3.2.0,M11.1.0",
        "JST-9",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",
    };

    lv_obj_t* tz_dd = lv_dropdown_create(tz_row);
    lv_dropdown_set_options(tz_dd, tz_opts);
    lv_obj_set_width(tz_dd, 260);
    lv_dropdown_set_selected(tz_dd, 1); // default CET
    lv_obj_add_event_cb(tz_dd, [](lv_event_t* e) {
        lv_obj_t* dd = lv_event_get_target(e);
        uint16_t sel = lv_dropdown_get_selected(dd);
        if (sel < 10) {
            setenv("TZ", tz_vals[sel], 1);
            tzset();
        }
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // ── Screen lock ───────────────────────────────────────────────────────────
    section_label(cont, "Screen Lock (fixes #76)");

    lv_obj_t* lock_row = lv_obj_create(cont);
    lv_obj_set_size(lock_row, LCD_W - 72, 50);
    lv_obj_set_style_bg_opa(lock_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(lock_row, 0, 0);
    lv_obj_set_flex_flow(lock_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(lock_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    row_label(lock_row, "Long-press (2s) to lock screen");

    lv_obj_t* lock_sw = lv_switch_create(lock_row);
    lv_obj_add_state(lock_sw, LV_STATE_CHECKED); // on by default

    // ── Firmware ──────────────────────────────────────────────────────────────
    section_label(cont, "Firmware");

    lv_obj_t* fw_row = lv_obj_create(cont);
    lv_obj_set_size(fw_row, LCD_W - 72, 50);
    lv_obj_set_style_bg_opa(fw_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fw_row, 0, 0);
    lv_obj_set_flex_flow(fw_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(fw_row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                           LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    char ver_buf[32];
    snprintf(ver_buf, sizeof(ver_buf), "Versie  v%s", OtaManager::current_version());
    row_label(fw_row, ver_buf);

    lv_obj_t* ota_btn = lv_btn_create(fw_row);
    lv_obj_set_size(ota_btn, 200, 38);
    lv_obj_set_style_bg_color(ota_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(ota_btn, [](lv_event_t*) {
        ScreenSettings::destroy();
        ScreenOta::show();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ota_lbl = lv_label_create(ota_btn);
    lv_label_set_text(ota_lbl, LV_SYMBOL_DOWNLOAD "  Bijwerken");
    lv_obj_center(ota_lbl);

    lv_scr_load(s_screen);
}

void ScreenSettings::destroy() {
    if (s_screen) { lv_obj_del(s_screen); s_screen = nullptr; }
    ScreenHome::create();
}
