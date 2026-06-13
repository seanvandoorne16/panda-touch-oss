#pragma once
#include <cstdint>
#include "esp_err.h"

enum class ChargeState { DISCHARGING, CHARGING, FULL };

class BatteryMonitor {
public:
    static BatteryMonitor& instance();

    // GPIO_NUM_4 = ADC channel on PandaTouch hardware (adjust if needed)
    esp_err_t init(int adc_gpio = 4, int charge_detect_gpio = 5);

    // 0-100 %
    int         level_pct()    const { return _pct;   }
    ChargeState charge_state() const { return _charge; }
    float       voltage_mv()   const { return _mv;    }

    // Call periodically (e.g. every 10 s) to refresh
    void update();

    // LVGL symbol string for the current level
    const char* lv_symbol() const;

private:
    BatteryMonitor() = default;

    int         _adc_gpio    = 4;
    int         _charge_gpio = 5;
    int         _pct         = -1;   // -1 = not yet read
    float       _mv          = 0.f;
    ChargeState _charge      = ChargeState::DISCHARGING;
    void*       _adc_handle  = nullptr;
    void*       _adc_unit    = nullptr;
    void*       _adc_chan    = nullptr;
    int         _adc_channel = 3;   // derived from GPIO via adc_oneshot_io_to_channel
    int         _adc_unit_id = 1;   // ADC_UNIT_1 or ADC_UNIT_2

    // Voltage → percentage lookup (LiPo 3.7 V nominal, 4.2 V full, 3.0 V empty)
    static int voltage_to_pct(float mv);
};
