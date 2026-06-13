#pragma once
#include "esp_err.h"
#include "lvgl.h"

// Hardware pin assignments for PandaTouch 5" 800x480
namespace pins {
    // RGB LCD (parallel)
    constexpr int LCD_HSYNC  = 39;
    constexpr int LCD_VSYNC  = 41;
    constexpr int LCD_DE     = 40;
    constexpr int LCD_PCLK   = 42;
    constexpr int LCD_R0=45, LCD_R1=48, LCD_R2=47, LCD_R3=21, LCD_R4=14;
    constexpr int LCD_G0=5,  LCD_G1=6,  LCD_G2=7,  LCD_G3=15, LCD_G4=16, LCD_G5=4;
    constexpr int LCD_B0=8,  LCD_B1=3,  LCD_B2=46, LCD_B3=9,  LCD_B4=1;
    constexpr int LCD_BL     = 2;     // backlight PWM

    // GT911 touch (I2C)
    constexpr int TOUCH_SDA  = 19;
    constexpr int TOUCH_SCL  = 20;
    constexpr int TOUCH_INT  = 18;
    constexpr int TOUCH_RST  = 38;

    // Battery ADC & charge detect
    constexpr int BAT_ADC    = 4;   // confirm from schematic
    constexpr int BAT_CHG    = 5;
}

constexpr int LCD_W = 800;
constexpr int LCD_H = 480;

class UiManager {
public:
    static UiManager& instance();

    esp_err_t init();

    // Set backlight 0-100%
    void set_brightness(uint8_t pct);

    // Must be called from main loop (or dedicated task) every ~5ms
    void tick();

    // LVGL mutex — acquire before any lv_ call from non-UI tasks
    void lock();
    void unlock();

    lv_disp_t* display() const { return _disp; }

private:
    UiManager() = default;
    esp_err_t init_lcd();
    esp_err_t init_touch();
    esp_err_t init_lvgl();

    static bool lvgl_flush_ready_cb(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*);
    static void lvgl_flush_cb(lv_disp_drv_t*, const lv_area_t*, lv_color_t*);
    static void lvgl_touch_cb(lv_indev_drv_t*, lv_indev_data_t*);

    void*       _panel      = nullptr;
    void*       _touch      = nullptr;
    lv_disp_t*  _disp       = nullptr;
    void*       _mutex      = nullptr;
    uint8_t     _brightness = 80;

    static constexpr size_t LVGL_BUF_LINES = 20;
};
