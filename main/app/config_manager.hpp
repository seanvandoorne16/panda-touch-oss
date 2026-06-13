#pragma once
#include <string>
#include <vector>
#include "printer_state.hpp"

struct WifiConfig {
    std::string ssid;
    std::string password;
};

struct PrinterConfig {
    std::string name;
    std::string ip;
    std::string serial;
    std::string access_code;
};

class ConfigManager {
public:
    static ConfigManager& instance();

    esp_err_t init();

    // WiFi
    WifiConfig  get_wifi() const;
    esp_err_t   save_wifi(const WifiConfig& cfg);

    // Printers
    std::vector<PrinterConfig> get_printers() const;
    esp_err_t   save_printer(const PrinterConfig& cfg);
    esp_err_t   remove_printer(const std::string& serial);

    // Display
    uint8_t     get_brightness() const;    // 10-100
    esp_err_t   save_brightness(uint8_t v);

    uint16_t    get_sleep_sec() const;     // 0=never
    esp_err_t   save_sleep_sec(uint16_t v);

private:
    ConfigManager() = default;
    static constexpr const char* NVS_NS = "pt_cfg";
};
