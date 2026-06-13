#pragma once
#include <string>
#include <functional>
#include <vector>
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
    // Fix for multi-AP / mesh roaming (issues #31, #360, #365).
    esp_err_t connect(const std::string& ssid, const std::string& password);

    esp_err_t disconnect();

    WifiState   state() const { return _state; }
    std::string ip()    const { return _ip; }

    // FIX H3: multi-observer pattern — adding a callback no longer drops previous ones
    void add_state_listener(WifiStateCallback cb) { _callbacks.push_back(std::move(cb)); }
    void clear_state_listeners()                  { _callbacks.clear(); }

    // Scan for networks (async, result delivered on scan-done event)
    using ScanCallback = std::function<void(const std::vector<std::string>& ssids)>;
    esp_err_t scan_async(ScanCallback cb);

private:
    WifiManager() = default;
    static void event_handler(void* arg, esp_event_base_t base,
                               int32_t id, void* data);
    void        on_event(esp_event_base_t base, int32_t id, void* data);
    void        set_state(WifiState s, const std::string& ip = "");

    WifiState                   _state = WifiState::DISCONNECTED;
    std::string                 _ip;
    std::vector<WifiStateCallback> _callbacks;
    ScanCallback                _scan_cb;
    int                         _retry = 0;
    static constexpr int        MAX_RETRY = 5;

    std::string _ssid;
    std::string _password;
};
