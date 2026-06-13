#pragma once
#include <string>
#include <functional>
#include <memory>
#include "printer_state.hpp"
#include "esp_err.h"

using PrinterStateCallback = std::function<void(const PrinterState&)>;

// One BambuClient per printer.  Communicates via MQTT over TLS on port 8883.
class BambuClient {
public:
    explicit BambuClient(const std::string& serial,
                         const std::string& ip,
                         const std::string& access_code);
    ~BambuClient();

    esp_err_t connect();
    void      disconnect();
    bool      connected() const { return _connected; }

    // Commands
    esp_err_t set_light(bool chamber, bool on);
    esp_err_t set_speed(int pct);       // 50/100/124/166
    esp_err_t pause_print();
    esp_err_t resume_print();
    esp_err_t stop_print();
    esp_err_t set_fan(const char* fan, int speed); // "cooling_fan","big_fan1","big_fan2"
    esp_err_t set_temperature(const char* target, float temp); // "nozzle"/"bed"

    // Current state snapshot (thread-safe copy)
    PrinterState state() const;

    // Register callback (called from MQTT task, so be quick or dispatch)
    void on_update(PrinterStateCallback cb) { _callback = cb; }

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

    void*  _mqtt_handle = nullptr;
    bool   _connected   = false;

    mutable PrinterState _state;
    PrinterStateCallback _callback;

    std::string _report_topic;
    std::string _request_topic;
};
