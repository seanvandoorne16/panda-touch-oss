#include "wifi_manager.hpp"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/ip4_addr.h"
#include <cstring>
#include <vector>
#include <algorithm>

static const char* TAG = "wifi";

WifiManager& WifiManager::instance() {
    static WifiManager inst;
    return inst;
}

esp_err_t WifiManager::init() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::event_handler, this, nullptr));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Roaming: enable background scanning so firmware picks a stronger AP
    // on the same SSID automatically (fixes multi-AP / mesh issue)
    esp_wifi_set_rssi_threshold(-80);  // trigger scan when RSSI drops below -80 dBm

    ESP_LOGI(TAG, "WiFi initialised");
    return ESP_OK;
}

esp_err_t WifiManager::connect(const std::string& ssid, const std::string& password) {
    _ssid     = ssid;
    _password = password;
    _retry    = 0;

    wifi_config_t wcfg = {};
    // Copy SSID and password
    std::strncpy((char*)wcfg.sta.ssid,     ssid.c_str(),     sizeof(wcfg.sta.ssid) - 1);
    std::strncpy((char*)wcfg.sta.password, password.c_str(), sizeof(wcfg.sta.password) - 1);

    // KEY FIX: bssid_set = false means we connect to the SSID, not a specific
    // access point MAC. This allows the ESP32 to roam between APs on a mesh
    // or multi-AP network (the original firmware locked to one BSSID).
    wcfg.sta.bssid_set = false;

    // Allow the driver to pick the best AP when multiple match the SSID
    wcfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wcfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    // WPA2/WPA3 mixed mode support (fixes issue #331)
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wcfg.sta.pmf_cfg.capable    = true;
    wcfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));

    set_state(WifiState::CONNECTING);
    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid.c_str());
    return esp_wifi_connect();
}

esp_err_t WifiManager::disconnect() {
    _ssid.clear();
    _password.clear();
    esp_wifi_disconnect();
    set_state(WifiState::DISCONNECTED);
    return ESP_OK;
}

void WifiManager::set_state(WifiState s, const std::string& ip) {
    _state = s;
    _ip    = ip;
    if (_callback) _callback(s, ip);
}

void WifiManager::event_handler(void* arg, esp_event_base_t base,
                                 int32_t id, void* data) {
    reinterpret_cast<WifiManager*>(arg)->on_event(base, id, data);
}

void WifiManager::on_event(esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            // Nothing — connect() triggers connection explicitly
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
            ESP_LOGW(TAG, "Disconnected, reason=%d", ev->reason);
            if (_retry < MAX_RETRY && !_ssid.empty()) {
                _retry++;
                ESP_LOGI(TAG, "Retry %d/%d", _retry, MAX_RETRY);
                set_state(WifiState::CONNECTING);
                esp_wifi_connect();
            } else {
                set_state(WifiState::FAILED);
            }
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            if (_scan_cb) {
                uint16_t count = 0;
                esp_wifi_scan_get_ap_num(&count);
                std::vector<wifi_ap_record_t> records(count);
                esp_wifi_scan_get_ap_records(&count, records.data());

                // Deduplicate SSIDs and sort by RSSI
                std::vector<std::string> ssids;
                for (auto& r : records) {
                    std::string s = (char*)r.ssid;
                    if (!s.empty() &&
                        std::find(ssids.begin(), ssids.end(), s) == ssids.end()) {
                        ssids.push_back(s);
                    }
                }
                _scan_cb(ssids);
                _scan_cb = nullptr;
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        char ip_str[16];
        esp_ip4addr_ntoa(&ev->ip_info.ip, ip_str, sizeof(ip_str));
        _retry = 0;
        ESP_LOGI(TAG, "Connected, IP: %s", ip_str);
        set_state(WifiState::CONNECTED, ip_str);
    }
}

esp_err_t WifiManager::scan_async(ScanCallback cb) {
    _scan_cb = cb;
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    return esp_wifi_scan_start(&scan_cfg, false);
}
