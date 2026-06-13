#include "screen_printer.hpp"
#include "screen_home.hpp"
#include "ui_manager.hpp"
#include "esp_log.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "screen_printer";

static lv_obj_t* s_screen      = nullptr;
static lv_obj_t* s_name_lbl    = nullptr;
static lv_obj_t* s_status_lbl  = nullptr;
static lv_obj_t* s_progress_bar = nullptr;
static lv_obj_t* s_pct_lbl     = nullptr;
static lv_obj_t* s_eta_lbl     = nullptr;
static lv_obj_t* s_layer_lbl   = nullptr;
static lv_obj_t* s_nozzle_lbl  = nullptr;
static lv_obj_t* s_bed_lbl     = nullptr;
static lv_obj_t* s_chamber_lbl = nullptr;
static lv_obj_t* s_file_lbl    = nullptr;
static lv_obj_t* s_pause_btn   = nullptr;
static lv_obj_t* s_stop_btn    = nullptr;
static lv_obj_t* s_light_btn   = nullptr;

static std::shared_ptr<BambuClient> s_client;

static lv_obj_t* make_temp_card(lv_obj_t* parent, const char* title,
                                  lv_obj_t** out_label) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 180, 80);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0f3460), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);

    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_12, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 6, 6);

    *out_label = lv_label_create(card);
    lv_label_set_text(*out_label, "--°C");
    lv_obj_set_style_text_color(*out_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(*out_label, &lv_font_montserrat_24, 0);
    lv_obj_align(*out_label, LV_ALIGN_CENTER, 0, 6);

    return card;
}

void ScreenPrinter::show(std::shared_ptr<BambuClient> client) {
    s_client = client;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d0d0d), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    PrinterState st = client->state();

    // ── Back button ───────────────────────────────────────────────────────────
    lv_obj_t* back_btn = lv_btn_create(s_screen);
    lv_obj_set_size(back_btn, 80, 36);
    lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back_btn, [](lv_event_t*) {
        ScreenPrinter::destroy();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* b_lbl = lv_label_create(back_btn);
    lv_label_set_text(b_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(b_lbl);

    // ── Printer name & status ─────────────────────────────────────────────────
    s_name_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_name_lbl, st.name.empty() ? st.serial.c_str() : st.name.c_str());
    lv_obj_set_style_text_color(s_name_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_name_lbl, &lv_font_montserrat_22, 0);
    lv_obj_align(s_name_lbl, LV_ALIGN_TOP_MID, 0, 12);

    s_status_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_status_lbl, st.status_str());
    lv_obj_set_style_text_font(s_status_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_lbl, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 40);

    // ── File name ─────────────────────────────────────────────────────────────
    s_file_lbl = lv_label_create(s_screen);
    lv_label_set_text(s_file_lbl, st.subtask_name.empty() ? "--" : st.subtask_name.c_str());
    lv_obj_set_style_text_color(s_file_lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(s_file_lbl, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(s_file_lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_file_lbl, 500);
    lv_obj_align(s_file_lbl, LV_ALIGN_TOP_MID, 0, 60);

    // ── Progress bar + % ─────────────────────────────────────────────────────
    s_progress_bar = lv_bar_create(s_screen);
    lv_obj_set_size(s_progress_bar, LCD_W - 60, 20);
    lv_obj_align(s_progress_bar, LV_ALIGN_TOP_MID, 0, 86);
    lv_bar_set_value(s_progress_bar, st.progress_pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        s_progress_bar, lv_palette_main(LV_PALETTE_GREEN),
        LV_PART_INDICATOR);

    s_pct_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_pct_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_LEFT, 30, 112);

    // ── ETA (fixes issue #182) ────────────────────────────────────────────────
    s_eta_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_eta_lbl, lv_palette_lighten(LV_PALETTE_CYAN, 1), 0);
    lv_obj_set_style_text_font(s_eta_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(s_eta_lbl, LV_ALIGN_TOP_RIGHT, -30, 112);

    // ── Layer counter ─────────────────────────────────────────────────────────
    s_layer_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_layer_lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
    lv_obj_set_style_text_font(s_layer_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_layer_lbl, LV_ALIGN_TOP_MID, 0, 112);

    // ── Temperature cards ─────────────────────────────────────────────────────
    lv_obj_t* temp_row = lv_obj_create(s_screen);
    lv_obj_set_size(temp_row, LCD_W - 20, 90);
    lv_obj_align(temp_row, LV_ALIGN_TOP_MID, 0, 140);
    lv_obj_set_style_bg_opa(temp_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(temp_row, 0, 0);
    lv_obj_set_flex_flow(temp_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(temp_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(temp_row, LV_OBJ_FLAG_SCROLLABLE);

    make_temp_card(temp_row, LV_SYMBOL_UPLOAD " Nozzle",  &s_nozzle_lbl);
    make_temp_card(temp_row, LV_SYMBOL_HOME   " Bed",     &s_bed_lbl);
    make_temp_card(temp_row, LV_SYMBOL_LOOP   " Chamber", &s_chamber_lbl);

    // ── Control buttons ───────────────────────────────────────────────────────
    lv_obj_t* ctrl_row = lv_obj_create(s_screen);
    lv_obj_set_size(ctrl_row, LCD_W - 20, 64);
    lv_obj_align(ctrl_row, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(ctrl_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctrl_row, 0, 0);
    lv_obj_set_flex_flow(ctrl_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctrl_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ctrl_row, LV_OBJ_FLAG_SCROLLABLE);

    // Pause / Resume
    s_pause_btn = lv_btn_create(ctrl_row);
    lv_obj_set_size(s_pause_btn, 160, 50);
    lv_obj_set_style_bg_color(s_pause_btn, lv_color_hex(0xf0a500), 0);
    lv_obj_add_event_cb(s_pause_btn, [](lv_event_t*) {
        if (!s_client) return;
        PrinterState st = s_client->state();
        if (st.status == PrinterStatus::PAUSED) s_client->resume_print();
        else                                    s_client->pause_print();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* p_lbl = lv_label_create(s_pause_btn);
    lv_label_set_text(p_lbl, LV_SYMBOL_PAUSE " Pause");
    lv_obj_center(p_lbl);

    // Stop
    s_stop_btn = lv_btn_create(ctrl_row);
    lv_obj_set_size(s_stop_btn, 160, 50);
    lv_obj_set_style_bg_color(s_stop_btn, lv_color_hex(0xc0392b), 0);
    lv_obj_add_event_cb(s_stop_btn, [](lv_event_t*) {
        // TODO: add confirmation dialog before stop
        if (s_client) s_client->stop_print();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* st_lbl2 = lv_label_create(s_stop_btn);
    lv_label_set_text(st_lbl2, LV_SYMBOL_STOP " Stop");
    lv_obj_center(st_lbl2);

    // Chamber light toggle
    s_light_btn = lv_btn_create(ctrl_row);
    lv_obj_set_size(s_light_btn, 160, 50);
    lv_obj_set_style_bg_color(s_light_btn, lv_color_hex(0x1a5276), 0);
    lv_obj_add_event_cb(s_light_btn, [](lv_event_t*) {
        if (!s_client) return;
        PrinterState cur = s_client->state();
        s_client->set_light(true, !cur.chamber_light);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* l_lbl = lv_label_create(s_light_btn);
    lv_label_set_text(l_lbl, LV_SYMBOL_IMAGE " Light");
    lv_obj_center(l_lbl);

    lv_scr_load(s_screen);

    // Hook live updates
    client->add_update_listener([](const PrinterState& s) {
        UiManager::instance().lock();
        ScreenPrinter::refresh(s);
        UiManager::instance().unlock();
    });

    refresh(st);
}

void ScreenPrinter::refresh(const PrinterState& st) {
    if (!s_screen) return;

    UiManager::instance().lock();

    lv_label_set_text(s_status_lbl, st.status_str());
    if (!st.subtask_name.empty())
        lv_label_set_text(s_file_lbl, st.subtask_name.c_str());

    lv_bar_set_value(s_progress_bar, st.progress_pct, LV_ANIM_ON);

    // Progress %
    char buf[64];
    snprintf(buf, sizeof(buf), "%d%%", st.progress_pct);
    lv_label_set_text(s_pct_lbl, buf);

    // ETA
    snprintf(buf, sizeof(buf), LV_SYMBOL_CLOCK " %s", st.remaining_str().c_str());
    lv_label_set_text(s_eta_lbl, buf);

    // Layer
    if (st.layer_total > 0) {
        snprintf(buf, sizeof(buf), "Layer %d / %d", st.layer_cur, st.layer_total);
        lv_label_set_text(s_layer_lbl, buf);
    }

    // Temperatures
    snprintf(buf, sizeof(buf), "%.0f / %.0f°C", st.nozzle_temp, st.nozzle_target);
    lv_label_set_text(s_nozzle_lbl, buf);
    snprintf(buf, sizeof(buf), "%.0f / %.0f°C", st.bed_temp, st.bed_target);
    lv_label_set_text(s_bed_lbl, buf);
    snprintf(buf, sizeof(buf), "%.0f°C", st.chamber_temp);
    lv_label_set_text(s_chamber_lbl, buf);

    // Pause button label
    lv_obj_t* p_lbl = lv_obj_get_child(s_pause_btn, 0);
    lv_label_set_text(p_lbl,
        st.status == PrinterStatus::PAUSED
            ? LV_SYMBOL_PLAY  " Resume"
            : LV_SYMBOL_PAUSE " Pause");

    // FIX M1: correct LVGL enable/disable API
    bool active = (st.status == PrinterStatus::PRINTING ||
                   st.status == PrinterStatus::PAUSED);
    if (active) {
        lv_obj_clear_state(s_pause_btn, LV_STATE_DISABLED);
        lv_obj_clear_state(s_stop_btn,  LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_pause_btn, LV_STATE_DISABLED);
        lv_obj_add_state(s_stop_btn,  LV_STATE_DISABLED);
    }

    UiManager::instance().unlock();
}

void ScreenPrinter::destroy() {
    if (s_client) {
        // Re-register home screen callback
        // (handled by ScreenHome when it becomes active again)
        s_client = nullptr;
    }
    if (s_screen) {
        lv_obj_del(s_screen);
        s_screen = nullptr;
    }
    ScreenHome::create();
}
