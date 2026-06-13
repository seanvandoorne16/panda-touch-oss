#include "screen_screensaver.hpp"
#include "ui_manager.hpp"
#include "sleep_manager.hpp"
#include "clock_manager.hpp"
#include <cstdio>

static lv_obj_t*  s_screen  = nullptr;
static lv_obj_t*  s_pct_lbl = nullptr;
static lv_obj_t*  s_eta_lbl = nullptr;
static lv_obj_t*  s_bar     = nullptr;
static lv_obj_t*  s_file    = nullptr;
static lv_obj_t*  s_clock   = nullptr;
static lv_timer_t* s_clock_timer = nullptr;

static bool s_active = false;

static void on_touch(lv_event_t*) {
    ScreenSaver::dismiss();
}

static void clock_tick(lv_timer_t*) {
    if (s_clock) {
        std::string t = ClockManager::instance().time_str();
        lv_label_set_text(s_clock, t.c_str());
    }
}

void ScreenSaver::show(const PrinterState& state) {
    if (s_active) { update(state); return; }
    s_active = true;

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // Dismiss on touch
    lv_obj_add_event_cb(s_screen, on_touch, LV_EVENT_CLICKED, nullptr);

    // Clock (large, centered top)
    s_clock = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_clock, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_clock, lv_color_hex(0x444444), 0);
    lv_obj_align(s_clock, LV_ALIGN_TOP_MID, 0, 40);
    lv_label_set_text(s_clock, ClockManager::instance().time_str().c_str());

    s_clock_timer = lv_timer_create(clock_tick, 30000, nullptr);

    // File name
    s_file = lv_label_create(s_screen);
    lv_obj_set_style_text_color(s_file, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(s_file, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(s_file, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(s_file, 600);
    lv_obj_align(s_file, LV_ALIGN_CENTER, 0, -60);

    // Progress bar
    s_bar = lv_bar_create(s_screen);
    lv_obj_set_size(s_bar, 600, 16);
    lv_obj_align(s_bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_palette_main(LV_PALETTE_GREEN), LV_PART_INDICATOR);

    // Progress % large
    s_pct_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_pct_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(s_pct_lbl, lv_color_hex(0x888888), 0);
    lv_obj_align(s_pct_lbl, LV_ALIGN_CENTER, -160, 50);

    // ETA
    s_eta_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_eta_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_eta_lbl, lv_color_hex(0x666666), 0);
    lv_obj_align(s_eta_lbl, LV_ALIGN_CENTER, 100, 52);

    // Tap to wake hint
    lv_obj_t* hint = lv_label_create(s_screen);
    lv_label_set_text(hint, "Tap to wake");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    update(state);
    lv_scr_load_anim(s_screen, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, false);
}

void ScreenSaver::update(const PrinterState& state) {
    if (!s_screen) return;

    lv_label_set_text(s_file,
        state.subtask_name.empty() ? "--" : state.subtask_name.c_str());

    lv_bar_set_value(s_bar, state.progress_pct, LV_ANIM_ON);

    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", state.progress_pct);
    lv_label_set_text(s_pct_lbl, buf);

    snprintf(buf, sizeof(buf), LV_SYMBOL_CLOCK " %s", state.remaining_str().c_str());
    lv_label_set_text(s_eta_lbl, buf);
}

void ScreenSaver::dismiss() {
    if (!s_active) return;
    s_active = false;
    SleepManager::instance().touch_activity();
    if (s_clock_timer) { lv_timer_del(s_clock_timer); s_clock_timer = nullptr; }
    if (s_screen) {
        lv_scr_load_anim(lv_scr_act(), LV_SCR_LOAD_ANIM_FADE_OUT, 300, 0, false);
        lv_obj_del_delayed(s_screen, 350);
        s_screen = nullptr;
    }
}

bool ScreenSaver::active() { return s_active; }
