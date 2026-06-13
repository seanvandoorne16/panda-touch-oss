#include "screen_ota.hpp"
#include "screen_settings.hpp"
#include "ota_manager.hpp"
#include "ui_manager.hpp"
#include "version.hpp"
#include "esp_log.h"
#include <cstdio>

static const char* TAG = "ota_ui";

static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_status_lbl  = nullptr;
static lv_obj_t* s_bar         = nullptr;
static lv_obj_t* s_update_btn  = nullptr;
static lv_obj_t* s_spinner     = nullptr;

static void set_status(const char* msg, lv_color_t color) {
    UiManager::instance().lock();
    lv_label_set_text(s_status_lbl, msg);
    lv_obj_set_style_text_color(s_status_lbl, color, 0);
    UiManager::instance().unlock();
}

static void on_update_clicked(lv_event_t*) {
    if (OtaManager::instance().is_updating()) return;

    // Scherm overschakelen naar voortgangsmodus
    UiManager::instance().lock();
    lv_obj_add_flag(s_update_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_bar,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_status_lbl, "Firmware downloaden...");
    lv_obj_set_style_text_color(s_status_lbl, lv_color_white(), 0);
    UiManager::instance().unlock();

    OtaManager::instance().start_update(
        FIRMWARE_OTA_URL,

        // Voortgang-callback (roept vanuit OTA-taak aan — LVGL lock nodig)
        [](int pct) {
            UiManager::instance().lock();
            if (s_bar) lv_bar_set_value(s_bar, pct, LV_ANIM_ON);
            char buf[32];
            snprintf(buf, sizeof(buf), "Downloaden... %d%%", pct);
            if (s_status_lbl) lv_label_set_text(s_status_lbl, buf);
            UiManager::instance().unlock();
        },

        // Gereed-callback
        [](bool success, const char* msg) {
            UiManager::instance().lock();
            if (s_spinner) lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
            if (success) {
                if (s_bar) lv_bar_set_value(s_bar, 100, LV_ANIM_OFF);
                set_status(msg, lv_palette_main(LV_PALETTE_GREEN));
            } else {
                if (s_bar) lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
                if (s_update_btn) lv_obj_clear_flag(s_update_btn, LV_OBJ_FLAG_HIDDEN);
                set_status(msg, lv_palette_main(LV_PALETTE_RED));
            }
            UiManager::instance().unlock();
        }
    );
}

void ScreenOta::show() {
    if (s_screen) return;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d0d0d), 0);

    // Terug-knop
    lv_obj_t* back = lv_btn_create(s_screen);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, [](lv_event_t*) {
        ScreenOta::destroy();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Terug");
    lv_obj_center(bl);

    // Titel
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, LV_SYMBOL_DOWNLOAD " Firmware-update");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    // Huidige versie
    char ver_buf[48];
    snprintf(ver_buf, sizeof(ver_buf), "Huidige versie:  v%s", OtaManager::current_version());
    lv_obj_t* ver_lbl = lv_label_create(s_screen);
    lv_label_set_text(ver_lbl, ver_buf);
    lv_obj_set_style_text_color(ver_lbl, lv_color_hex(0xaaaaaa), 0);
    lv_obj_set_style_text_font(ver_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(ver_lbl, LV_ALIGN_CENTER, 0, -80);

    // Updatebron-label
    lv_obj_t* src_lbl = lv_label_create(s_screen);
    lv_label_set_text(src_lbl, "Bron: GitHub Releases (laatste versie)");
    lv_obj_set_style_text_color(src_lbl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(src_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(src_lbl, LV_ALIGN_CENTER, 0, -50);

    // Waarschuwing
    lv_obj_t* warn = lv_label_create(s_screen);
    lv_label_set_text(warn, LV_SYMBOL_WARNING "  Onderbreek de stroom niet tijdens het bijwerken");
    lv_obj_set_style_text_color(warn, lv_palette_main(LV_PALETTE_ORANGE), 0);
    lv_obj_set_style_text_font(warn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(warn, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(warn, LV_ALIGN_CENTER, 0, -10);

    // Voortgangsbalk (verborgen tot update start)
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_size(s_bar, 500, 28);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 40);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);

    // Spinner
    s_spinner = lv_spinner_create(s_screen, 1000, 60);
    lv_obj_set_size(s_spinner, 36, 36);
    lv_obj_align(s_spinner, LV_ALIGN_CENTER, 270, 40);
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);

    // Status-label
    s_status_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_status_lbl, "");
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_CENTER, 0, 85);

    // Bijwerken-knop
    s_update_btn = lv_btn_create(s_screen);
    lv_obj_set_size(s_update_btn, 260, 56);
    lv_obj_align(s_update_btn, LV_ALIGN_CENTER, 0, 150);
    lv_obj_set_style_bg_color(s_update_btn, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_add_event_cb(s_update_btn, on_update_clicked, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* btn_lbl = lv_label_create(s_update_btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_DOWNLOAD "  Nu bijwerken");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(btn_lbl);

    lv_scr_load(s_screen);
}

void ScreenOta::destroy() {
    if (s_screen) {
        lv_obj_del(s_screen);
        s_screen     = nullptr;
        s_status_lbl = nullptr;
        s_bar        = nullptr;
        s_update_btn = nullptr;
        s_spinner    = nullptr;
    }
    ScreenSettings::show();
}
