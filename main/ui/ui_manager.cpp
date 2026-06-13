#include "ui_manager.hpp"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_lcd_touch_gt911.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include <cstring>

static const char* TAG = "ui";

UiManager& UiManager::instance() {
    static UiManager inst;
    return inst;
}

// ── Backlight ─────────────────────────────────────────────────────────────────

static void backlight_init(int gpio) {
    ledc_timer_config_t t = {};
    t.speed_mode      = LEDC_LOW_SPEED_MODE;
    t.timer_num       = LEDC_TIMER_0;
    t.duty_resolution = LEDC_TIMER_10_BIT;
    t.freq_hz         = 5000;
    t.clk_cfg         = LEDC_AUTO_CLK;
    ledc_timer_config(&t);

    ledc_channel_config_t ch = {};
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel    = LEDC_CHANNEL_0;
    ch.timer_sel  = LEDC_TIMER_0;
    ch.gpio_num   = gpio;
    ch.duty       = 820;  // ~80%
    ch.hpoint     = 0;
    ledc_channel_config(&ch);
}

void UiManager::set_brightness(uint8_t pct) {
    _brightness = pct;
    uint32_t duty = (pct * 1023) / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ── LCD (RGB parallel) ────────────────────────────────────────────────────────

esp_err_t UiManager::init_lcd() {
    esp_lcd_rgb_panel_config_t cfg = {};
    cfg.data_width                 = 16;
    cfg.psram_trans_align          = 64;
    cfg.num_fbs                    = 2;         // double buffer (no tearing)
    cfg.clk_src                    = LCD_CLK_SRC_DEFAULT;
    cfg.disp_gpio_num              = GPIO_NUM_NC;
    cfg.pclk_gpio_num              = pins::LCD_PCLK;
    cfg.vsync_gpio_num             = pins::LCD_VSYNC;
    cfg.hsync_gpio_num             = pins::LCD_HSYNC;
    cfg.de_gpio_num                = pins::LCD_DE;
    cfg.data_gpio_nums[0]          = pins::LCD_B0;
    cfg.data_gpio_nums[1]          = pins::LCD_B1;
    cfg.data_gpio_nums[2]          = pins::LCD_B2;
    cfg.data_gpio_nums[3]          = pins::LCD_B3;
    cfg.data_gpio_nums[4]          = pins::LCD_B4;
    cfg.data_gpio_nums[5]          = pins::LCD_G0;
    cfg.data_gpio_nums[6]          = pins::LCD_G1;
    cfg.data_gpio_nums[7]          = pins::LCD_G2;
    cfg.data_gpio_nums[8]          = pins::LCD_G3;
    cfg.data_gpio_nums[9]          = pins::LCD_G4;
    cfg.data_gpio_nums[10]         = pins::LCD_G5;
    cfg.data_gpio_nums[11]         = pins::LCD_R0;
    cfg.data_gpio_nums[12]         = pins::LCD_R1;
    cfg.data_gpio_nums[13]         = pins::LCD_R2;
    cfg.data_gpio_nums[14]         = pins::LCD_R3;
    cfg.data_gpio_nums[15]         = pins::LCD_R4;

    // Timing for 800x480 @ ~30 fps
    cfg.timings.pclk_hz            = 16000000;
    cfg.timings.h_res              = LCD_W;
    cfg.timings.v_res              = LCD_H;
    cfg.timings.hsync_back_porch   = 40;
    cfg.timings.hsync_front_porch  = 20;
    cfg.timings.hsync_pulse_width  = 10;
    cfg.timings.vsync_back_porch   = 8;
    cfg.timings.vsync_front_porch  = 4;
    cfg.timings.vsync_pulse_width  = 2;
    cfg.timings.flags.pclk_active_neg = true;

    cfg.flags.fb_in_psram          = true;
    cfg.flags.double_fb            = true;

    esp_lcd_panel_handle_t* ph = reinterpret_cast<esp_lcd_panel_handle_t*>(&_panel);
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, ph), TAG, "rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*ph), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ph),  TAG, "panel init");

    backlight_init(pins::LCD_BL);
    return ESP_OK;
}

// ── Touch (GT911 over I2C) ────────────────────────────────────────────────────

esp_err_t UiManager::init_touch() {
    i2c_config_t i2c_cfg = {};
    i2c_cfg.mode             = I2C_MODE_MASTER;
    i2c_cfg.sda_io_num       = pins::TOUCH_SDA;
    i2c_cfg.scl_io_num       = pins::TOUCH_SCL;
    i2c_cfg.sda_pullup_en    = true;
    i2c_cfg.scl_pullup_en    = true;
    i2c_cfg.master.clk_speed = 400000;
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr            = ESP_LCD_TOUCH_IO_I2C_GT911_ADDRESS;
    io_cfg.scl_speed_hz        = 400000;

    esp_lcd_panel_io_handle_t tp_io;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_NUM_0,
                                              &io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max              = LCD_W;
    tp_cfg.y_max              = LCD_H;
    tp_cfg.rst_gpio_num       = (gpio_num_t)pins::TOUCH_RST;
    tp_cfg.int_gpio_num       = (gpio_num_t)pins::TOUCH_INT;
    tp_cfg.levels.reset       = 0;
    tp_cfg.flags.swap_xy      = false;
    tp_cfg.flags.mirror_x     = false;
    tp_cfg.flags.mirror_y     = false;

    esp_lcd_touch_handle_t* th = reinterpret_cast<esp_lcd_touch_handle_t*>(&_touch);
    return esp_lcd_touch_new_i2c_gt911(tp_io, &tp_cfg, th);
}

// ── LVGL ─────────────────────────────────────────────────────────────────────

static lv_color_t* s_lvgl_buf1 = nullptr;
static lv_color_t* s_lvgl_buf2 = nullptr;

void UiManager::lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area,
                               lv_color_t* color) {
    auto* panel = reinterpret_cast<esp_lcd_panel_handle_t>(drv->user_data);
    esp_lcd_panel_draw_bitmap(panel,
        area->x1, area->y1, area->x2 + 1, area->y2 + 1, color);
    lv_disp_flush_ready(drv);
}

void UiManager::lvgl_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    auto* touch = reinterpret_cast<esp_lcd_touch_handle_t>(drv->user_data);
    uint16_t x, y, strength;
    uint8_t  count = 0;
    esp_lcd_touch_read_data(touch);
    bool pressed = esp_lcd_touch_get_coordinates(touch, &x, &y, &strength, &count, 1)
                   && count > 0;
    data->state   = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;
}

esp_err_t UiManager::init_lvgl() {
    lv_init();

    size_t buf_size = LCD_W * LVGL_BUF_LINES * sizeof(lv_color_t);
    s_lvgl_buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    s_lvgl_buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!s_lvgl_buf1 || !s_lvgl_buf2) return ESP_ERR_NO_MEM;

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_lvgl_buf1, s_lvgl_buf2, LCD_W * LVGL_BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res   = LCD_W;
    disp_drv.ver_res   = LCD_H;
    disp_drv.flush_cb  = lvgl_flush_cb;
    disp_drv.draw_buf  = &draw_buf;
    disp_drv.user_data = _panel;
    _disp = lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type      = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb   = lvgl_touch_cb;
    indev_drv.user_data = _touch;
    lv_indev_drv_register(&indev_drv);

    _mutex = xSemaphoreCreateMutex();
    return ESP_OK;
}

// ── Public init ───────────────────────────────────────────────────────────────

esp_err_t UiManager::init() {
    ESP_RETURN_ON_ERROR(init_lcd(),   TAG, "init_lcd");
    ESP_RETURN_ON_ERROR(init_touch(), TAG, "init_touch");
    ESP_RETURN_ON_ERROR(init_lvgl(),  TAG, "init_lvgl");
    ESP_LOGI(TAG, "Display %dx%d ready", LCD_W, LCD_H);
    return ESP_OK;
}

void UiManager::tick() {
    lv_timer_handler();
}

void UiManager::lock() {
    xSemaphoreTake((SemaphoreHandle_t)_mutex, portMAX_DELAY);
}

void UiManager::unlock() {
    xSemaphoreGive((SemaphoreHandle_t)_mutex);
}
