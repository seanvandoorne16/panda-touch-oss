#pragma once
#include <string>
#include <functional>
#include "esp_err.h"

enum class WifiState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    FAILED,
};

using WifiStateCallback = std::function<void(WifiState, const std::string& ip)>;

class WifiManager {
public:
    static WifiManager& instance();

    esp_err_t init();

    // Connect by SSID only — never locks to a specific BSSID.
    // This is the fix for the multi-AP / mesh roaming issue.
    esp_err_t connect(const std::string& ssid, const std::string& password);

    esp_err_t disconnect();

    WifiState  state() const { return _state; }
    std::string ip()   const { return _ip; }

    // Called from any thread when state changes
    void on_state_change(WifiStateCallback cb) { _callback = cb; }

    // Scan for networks (async, calls back on completion)
    using ScanCallback = std::function<void(const std::vector<std::string>& ssids)>;
    esp_err_t scan_async(ScanCallback cb);

private:
    WifiManager() = default;
    static void event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data);
    void        on_event(esp_event_base_t base, int32_t id, void* data);
    void        set_state(WifiState s, const std::string& ip = "");

    WifiState        _state    = WifiState::DISCONNECTED;
    std::string      _ip;
    WifiStateCallback _callback;
    ScanCallback     _scan_cb;
    int              _retry    = 0;
    static constexpr int MAX_RETRY = 5;

    std::string _ssid;
    std::string _password;
};
