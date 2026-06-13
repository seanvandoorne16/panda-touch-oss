#include "config_manager.hpp"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <cstring>

static const char* TAG = "cfg";

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

esp_err_t ConfigManager::init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased, reinitialising");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

// ── helpers ──────────────────────────────────────────────────────────────────

static esp_err_t nvs_get_str_alloc(nvs_handle_t h, const char* key, std::string& out) {
    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK) return err;
    out.resize(len);
    return nvs_get_str(h, key, out.data(), &len);
}

// ── WiFi ─────────────────────────────────────────────────────────────────────

WifiConfig ConfigManager::get_wifi() const {
    WifiConfig cfg;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return cfg;
    nvs_get_str_alloc(h, "wifi_ssid", cfg.ssid);
    nvs_get_str_alloc(h, "wifi_pass", cfg.password);
    nvs_close(h);
    return cfg;
}

esp_err_t ConfigManager::save_wifi(const WifiConfig& cfg) {
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_set_str(h, "wifi_ssid", cfg.ssid.c_str());
    nvs_set_str(h, "wifi_pass", cfg.password.c_str());
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ── Printers (stored as JSON array in NVS) ───────────────────────────────────

static const char* PRINTERS_KEY = "printers_json";

std::vector<PrinterConfig> ConfigManager::get_printers() const {
    std::vector<PrinterConfig> result;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return result;

    std::string json;
    nvs_get_str_alloc(h, PRINTERS_KEY, json);
    nvs_close(h);

    if (json.empty()) return result;

    cJSON* arr = cJSON_Parse(json.c_str());
    if (!arr) return result;

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        PrinterConfig pc;
        cJSON* v;
        if ((v = cJSON_GetObjectItem(item, "name")))         pc.name        = v->valuestring;
        if ((v = cJSON_GetObjectItem(item, "ip")))           pc.ip          = v->valuestring;
        if ((v = cJSON_GetObjectItem(item, "serial")))       pc.serial      = v->valuestring;
        if ((v = cJSON_GetObjectItem(item, "access_code")))  pc.access_code = v->valuestring;
        result.push_back(pc);
    }
    cJSON_Delete(arr);
    return result;
}

static esp_err_t printers_to_json_str(const std::vector<PrinterConfig>& printers, std::string& out) {
    cJSON* arr = cJSON_CreateArray();
    for (const auto& pc : printers) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name",        pc.name.c_str());
        cJSON_AddStringToObject(item, "ip",          pc.ip.c_str());
        cJSON_AddStringToObject(item, "serial",      pc.serial.c_str());
        cJSON_AddStringToObject(item, "access_code", pc.access_code.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    char* s = cJSON_PrintUnformatted(arr);
    out = s ? s : "[]";
    cJSON_free(s);
    cJSON_Delete(arr);
    return ESP_OK;
}

esp_err_t ConfigManager::save_printer(const PrinterConfig& cfg) {
    auto printers = get_printers();
    // Update existing or append
    bool found = false;
    for (auto& p : printers) {
        if (p.serial == cfg.serial) { p = cfg; found = true; break; }
    }
    if (!found) printers.push_back(cfg);

    std::string json;
    printers_to_json_str(printers, json);

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_set_str(h, PRINTERS_KEY, json.c_str());
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t ConfigManager::remove_printer(const std::string& serial) {
    auto printers = get_printers();
    printers.erase(
        std::remove_if(printers.begin(), printers.end(),
            [&](const PrinterConfig& p) { return p.serial == serial; }),
        printers.end());

    std::string json;
    printers_to_json_str(printers, json);

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_set_str(h, PRINTERS_KEY, json.c_str());
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ── Display settings ─────────────────────────────────────────────────────────

uint8_t ConfigManager::get_brightness() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 80;
    uint8_t v = 80;
    nvs_get_u8(h, "brightness", &v);
    nvs_close(h);
    return v;
}

esp_err_t ConfigManager::save_brightness(uint8_t v) {
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_set_u8(h, "brightness", v);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

uint16_t ConfigManager::get_sleep_sec() const {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return 300;
    uint16_t v = 300;
    nvs_get_u16(h, "sleep_sec", &v);
    nvs_close(h);
    return v;
}

esp_err_t ConfigManager::save_sleep_sec(uint16_t v) {
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs_open");
    nvs_set_u16(h, "sleep_sec", v);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}
