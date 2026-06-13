#include "screen_ams.hpp"
#include "screen_printer.hpp"
#include "screen_home.hpp"
#include "ui_manager.hpp"
#include "esp_log.h"
#include <cstdio>
#include <vector>

static const char* TAG = "ams";

static lv_obj_t* s_screen = nullptr;
static std::shared_ptr<BambuClient> s_client;
static std::vector<lv_obj_t*> s_slot_cards;

static uint32_t hex_color(const std::string& hex) {
    if (hex.size() < 6) return 0xAAAAAA;
    unsigned long v = strtoul(hex.c_str(), nullptr, 16);
    // Bambu sends RRGGBBAA — strip alpha
    return (v >> 8) & 0xFFFFFF;
}

static lv_obj_t* make_slot_card(lv_obj_t* parent, const AmsSlot& slot) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, 160, 200);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x333355), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    // Colour swatch
    lv_obj_t* swatch = lv_obj_create(card);
    lv_obj_set_size(swatch, 80, 80);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(hex_color(slot.color)), 0);
    lv_obj_set_style_border_width(swatch, 2, 0);
    lv_obj_set_style_border_color(swatch, lv_color_white(), 0);
    lv_obj_align(swatch, LV_ALIGN_TOP_MID, 0, 6);

    // Slot ID label
    char id_buf[4];
    snprintf(id_buf, sizeof(id_buf), "%d", slot.id + 1);
    lv_obj_t* id_lbl = lv_label_create(swatch);
    lv_label_set_text(id_lbl, id_buf);
    lv_obj_set_style_text_color(id_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(id_lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(id_lbl);

    // Material
    lv_obj_t* mat = lv_label_create(card);
    lv_label_set_text(mat, slot.material.empty() ? "--" : slot.material.c_str());
    lv_obj_set_style_text_color(mat, lv_color_white(), 0);
    lv_obj_set_style_text_font(mat, &lv_font_montserrat_14, 0);
    lv_obj_align(mat, LV_ALIGN_TOP_MID, 0, 96);

    // Remaining % bar (fixes issue #212)
    lv_obj_t* bar = lv_bar_create(card);
    lv_obj_set_size(bar, 130, 14);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 126);
    int remain = (slot.remain_pct >= 0) ? slot.remain_pct : 0;
    lv_bar_set_value(bar, remain, LV_ANIM_OFF);

    // Colour the bar based on remaining amount
    lv_color_t bar_color = remain > 30
        ? lv_palette_main(LV_PALETTE_GREEN)
        : remain > 10
            ? lv_palette_main(LV_PALETTE_ORANGE)
            : lv_palette_main(LV_PALETTE_RED);
    lv_obj_set_style_bg_color(bar, bar_color, LV_PART_INDICATOR);

    // Remaining % text
    lv_obj_t* pct_lbl = lv_label_create(card);
    if (slot.remain_pct >= 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", slot.remain_pct);
        lv_label_set_text(pct_lbl, buf);
    } else {
        lv_label_set_text(pct_lbl, "--");
    }
    lv_obj_set_style_text_color(pct_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(pct_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(pct_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    return card;
}

void ScreenAms::show(std::shared_ptr<BambuClient> client) {
    s_client = client;
    PrinterState st = client->state();

    s_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0d0d0d), 0);

    // Back button
    lv_obj_t* back = lv_btn_create(s_screen);
    lv_obj_set_size(back, 80, 36);
    lv_obj_align(back, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(back, [](lv_event_t*) {
        ScreenAms::destroy();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* bl = lv_label_create(back);
    lv_label_set_text(bl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(bl);

    // Title
    lv_obj_t* title = lv_label_create(s_screen);
    lv_label_set_text(title, "AMS Filament");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    if (st.ams_trays.empty()) {
        lv_obj_t* no_ams = lv_label_create(s_screen);
        lv_label_set_text(no_ams, "No AMS detected");
        lv_obj_set_style_text_color(no_ams, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_center(no_ams);
        lv_scr_load(s_screen);
        return;
    }

    // Scrollable tray container
    lv_obj_t* scroll = lv_obj_create(s_screen);
    lv_obj_set_size(scroll, LCD_W, LCD_H - 60);
    lv_obj_align(scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_flex_flow(scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(scroll, 12, 0);
    lv_obj_set_style_pad_all(scroll, 12, 0);

    s_slot_cards.clear();

    for (const auto& tray : st.ams_trays) {
        // Tray header
        lv_obj_t* tray_lbl = lv_label_create(scroll);
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "AMS %d  |  Humidity: %d/5  |  %d°C",
                 tray.id + 1, tray.humidity, tray.temp);
        lv_label_set_text(tray_lbl, tbuf);
        lv_obj_set_style_text_color(tray_lbl, lv_palette_lighten(LV_PALETTE_GREY, 1), 0);
        lv_obj_set_style_text_font(tray_lbl, &lv_font_montserrat_12, 0);

        // Slot row
        lv_obj_t* row = lv_obj_create(scroll);
        lv_obj_set_size(row, LCD_W - 24, 210);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(row, 10, 0);

        for (const auto& slot : tray.slots) {
            lv_obj_t* card = make_slot_card(row, slot);
            s_slot_cards.push_back(card);
        }
    }

    lv_scr_load(s_screen);

    client->on_update([](const PrinterState& s) {
        ScreenAms::refresh(s);
    });
}

void ScreenAms::refresh(const PrinterState& state) {
    // Simple approach: rebuild if slot count changed, else update bars
    if (!s_screen) return;
    // For simplicity: let screen_ams be recreated on next open
    // (AMS data doesn't change rapidly enough to need live partial updates)
}

void ScreenAms::destroy() {
    s_slot_cards.clear();
    if (s_screen) { lv_obj_del(s_screen); s_screen = nullptr; }
    // FIX C3: capture client before clearing, then navigate
    auto client = std::move(s_client);
    s_client = nullptr;
    if (client) ScreenPrinter::show(client);
    else        ScreenHome::create();
}
