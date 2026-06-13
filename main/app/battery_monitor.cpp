#include "battery_monitor.hpp"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "lvgl.h"
#include <algorithm>

static const char* TAG = "batt";

BatteryMonitor& BatteryMonitor::instance() {
    static BatteryMonitor inst;
    return inst;
}

esp_err_t BatteryMonitor::init(int adc_gpio, int charge_detect_gpio) {
    _adc_gpio    = adc_gpio;
    _charge_gpio = charge_detect_gpio;

    // Configure charge detect pin as input with pull-up
    gpio_config_t io = {};
    io.intr_type    = GPIO_INTR_DISABLE;
    io.mode         = GPIO_MODE_INPUT;
    io.pin_bit_mask = 1ULL << charge_detect_gpio;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);

    // FIX H5: map GPIO to ADC unit+channel at runtime, not hardcoded
    adc_unit_t unit_id;
    adc_channel_t chan;
    if (adc_oneshot_io_to_channel(adc_gpio, &unit_id, &chan) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC-capable pin", adc_gpio);
        return ESP_ERR_INVALID_ARG;
    }
    _adc_channel = (int)chan;
    _adc_unit_id = (int)unit_id;

    // ADC oneshot init
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = unit_id;

    adc_oneshot_unit_handle_t* handle =
        reinterpret_cast<adc_oneshot_unit_handle_t*>(&_adc_unit);
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, handle));

    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.bitwidth = ADC_BITWIDTH_12;
    chan_cfg.atten    = ADC_ATTEN_DB_12;   // 0-3.9 V range

    adc_oneshot_chan_handle_t* ch_handle =
        reinterpret_cast<adc_oneshot_chan_handle_t*>(&_adc_chan);
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        *handle, chan, &chan_cfg));

    update();
    ESP_LOGI(TAG, "Battery: %.0f mV  %d%%", _mv, _pct);
    return ESP_OK;
}

void BatteryMonitor::update() {
    auto* handle = reinterpret_cast<adc_oneshot_unit_handle_t*>(&_adc_unit);
    if (!*handle) return;

    // Average 8 samples to reduce noise
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        adc_oneshot_read(*handle, (adc_channel_t)_adc_channel, &raw);
        sum += raw;
    }
    int raw_avg = sum / 8;

    // Convert raw to mV (12-bit ADC, 3.9V range, voltage divider ×2 on hardware)
    float adc_mv = (raw_avg / 4095.f) * 3900.f;
    _mv = adc_mv * 2.f;   // voltage divider compensation

    _pct = voltage_to_pct(_mv);

    // Charge state via GPIO (LOW = charging on typical TP4056-based circuits)
    bool charge_pin_low = gpio_get_level((gpio_num_t)_charge_gpio) == 0;
    if (charge_pin_low) {
        _charge = (_pct >= 99) ? ChargeState::FULL : ChargeState::CHARGING;
    } else {
        _charge = ChargeState::DISCHARGING;
    }
}

// LiPo discharge curve lookup table (voltage mV → percent)
int BatteryMonitor::voltage_to_pct(float mv) {
    static const struct { int mv; int pct; } table[] = {
        {4200, 100}, {4150, 95}, {4100, 90}, {4050, 85},
        {4000, 80},  {3950, 75}, {3900, 70}, {3850, 65},
        {3800, 60},  {3750, 55}, {3700, 50}, {3650, 45},
        {3600, 40},  {3550, 35}, {3500, 30}, {3450, 25},
        {3400, 20},  {3350, 15}, {3300, 10}, {3200,  5},
        {3000,   0},
    };
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    for (int i = 0; i < (int)(sizeof(table)/sizeof(table[0])) - 1; i++) {
        if (mv >= table[i+1].mv) {
            float ratio = (mv - table[i+1].mv) /
                          (float)(table[i].mv - table[i+1].mv);
            return table[i+1].pct + (int)(ratio * (table[i].pct - table[i+1].pct));
        }
    }
    return 0;
}

const char* BatteryMonitor::lv_symbol() const {
    if (_charge == ChargeState::CHARGING || _charge == ChargeState::FULL)
        return LV_SYMBOL_CHARGE;
    if (_pct < 0)  return LV_SYMBOL_BATTERY_EMPTY;
    if (_pct < 20) return LV_SYMBOL_BATTERY_1;
    if (_pct < 50) return LV_SYMBOL_BATTERY_2;
    if (_pct < 80) return LV_SYMBOL_BATTERY_3;
    return LV_SYMBOL_BATTERY_FULL;
}
