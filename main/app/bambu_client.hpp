#pragma once
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "printer_state.hpp"
#include "esp_err.h"

using PrinterStateCallback = std::function<void(const PrinterState&)>;

// One BambuClient per printer. Communicates via MQTT over TLS on port 8883.
class BambuClient {
public:
    explicit BambuClient(const std::string& serial,
                         const std::string& ip,
                         const std::string& access_code,
                         const std::string& name = "");
    ~BambuClient();

    esp_err_t connect();
    void      disconnect();
    bool      connected() const { return _connected; }

    // Commands
    esp_err_t set_light(bool chamber, bool on);
    esp_err_t set_speed(int pct);
    esp_err_t pause_print();
    esp_err_t resume_print();
    esp_err_t stop_print();
    esp_err_t set_fan(const char* fan, int speed);
    esp_err_t set_temperature(const char* target, float temp);
    esp_err_t home_axis(bool x, bool y, bool z);
    esp_err_t move_axis(char axis, float dist_mm, float speed_mmps);
    esp_err_t emergency_stop();
    esp_err_t send_gcode(const char* gcode);

    // Thread-safe state snapshot (FIX C5: mutex protected)
    PrinterState state() const;

    // FIX H3: multi-observer — registering doesn't drop previous callbacks
    void add_update_listener(PrinterStateCallback cb);

private:
    static void mqtt_event_handler(void* arg, esp_event_base_t base,
                                    int32_t id, void* data);
    void on_mqtt_event(int32_t id, void* data);
    void parse_report(const char* payload, int len);
    void parse_print(cJSON* print);
    void parse_ams(cJSON* ams);
    esp_err_t publish(const char* json);

    std::string _serial;
    std::string _ip;
    std::string _access_code;
    std::string _client_id;   // FIX C2: stable storage for MQTT client_id

    void*  _mqtt_handle = nullptr;
    bool   _connected   = false;

    mutable std::mutex           _state_mutex;  // FIX C5: data race guard
    PrinterState                 _state;
    std::vector<PrinterStateCallback> _callbacks;

    std::string _report_topic;
    std::string _request_topic;
};
