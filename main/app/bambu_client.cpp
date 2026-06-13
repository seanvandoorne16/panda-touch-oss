#include "bambu_client.hpp"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <cstring>
#include <mutex>

static const char* TAG = "bambu";

// Bambu printers use self-signed TLS — skip server cert verification
// (same as official Bambu Studio behaviour on LAN)
static const char* BAMBU_TLS_SKIP = nullptr;

BambuClient::BambuClient(const std::string& serial,
                         const std::string& ip,
                         const std::string& access_code)
    : _serial(serial), _ip(ip), _access_code(access_code)
{
    _report_topic  = "device/" + serial + "/report";
    _request_topic = "device/" + serial + "/request";
    _state.serial  = serial;
    _state.ip      = ip;
}

BambuClient::~BambuClient() {
    disconnect();
}

esp_err_t BambuClient::connect() {
    std::string uri = "mqtts://" + _ip + ":8883";

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri            = uri.c_str();
    cfg.broker.verification.skip_cert_common_name_check = true;
    cfg.broker.verification.use_global_ca_store         = false;
    // Bambu uses self-signed cert; disable verification (LAN-only use)
    cfg.broker.verification.certificate                 = nullptr;

    cfg.credentials.username          = "bblp";
    cfg.credentials.authentication.password = _access_code.c_str();
    cfg.credentials.client_id         = ("pt_" + _serial).c_str();

    cfg.session.keepalive             = 10;
    cfg.network.reconnect_timeout_ms  = 5000;
    cfg.network.timeout_ms            = 10000;
    cfg.task.stack_size               = 8192;

    _mqtt_handle = esp_mqtt_client_init(&cfg);
    if (!_mqtt_handle) return ESP_FAIL;

    esp_mqtt_client_register_event(
        (esp_mqtt_client_handle_t)_mqtt_handle,
        MQTT_EVENT_ANY,
        &BambuClient::mqtt_event_handler, this);

    return esp_mqtt_client_start((esp_mqtt_client_handle_t)_mqtt_handle);
}

void BambuClient::disconnect() {
    if (_mqtt_handle) {
        esp_mqtt_client_stop((esp_mqtt_client_handle_t)_mqtt_handle);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t)_mqtt_handle);
        _mqtt_handle = nullptr;
        _connected   = false;
    }
}

// ── MQTT events ───────────────────────────────────────────────────────────────

void BambuClient::mqtt_event_handler(void* arg, esp_event_base_t base,
                                      int32_t id, void* data) {
    reinterpret_cast<BambuClient*>(arg)->on_mqtt_event(id, data);
}

void BambuClient::on_mqtt_event(int32_t id, void* data) {
    auto* ev = static_cast<esp_mqtt_event_t*>(data);
    switch (id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "[%s] Connected", _serial.c_str());
            _connected = true;
            _state.online = true;
            esp_mqtt_client_subscribe(
                (esp_mqtt_client_handle_t)_mqtt_handle,
                _report_topic.c_str(), 0);

            // Request full status immediately after connect
            publish("{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "[%s] Disconnected", _serial.c_str());
            _connected    = false;
            _state.online = false;
            _state.status = PrinterStatus::OFFLINE;
            if (_callback) _callback(_state);
            break;

        case MQTT_EVENT_DATA:
            if (ev->data && ev->data_len > 0) {
                parse_report(ev->data, ev->data_len);
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "[%s] MQTT error", _serial.c_str());
            break;

        default:
            break;
    }
}

// ── Report parsing ────────────────────────────────────────────────────────────

static float json_float(cJSON* obj, const char* key, float def = 0.f) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsNumber(v)) ? (float)v->valuedouble : def;
}

static int json_int(cJSON* obj, const char* key, int def = 0) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsNumber(v)) ? v->valueint : def;
}

static const char* json_str(cJSON* obj, const char* key, const char* def = "") {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsString(v)) ? v->valuestring : def;
}

void BambuClient::parse_report(const char* payload, int len) {
    cJSON* root = cJSON_ParseWithLength(payload, len);
    if (!root) return;

    cJSON* print = cJSON_GetObjectItem(root, "print");
    if (print) parse_print(print);

    cJSON* ams = cJSON_GetObjectItem(root, "ams");
    if (ams) parse_ams(ams);

    cJSON_Delete(root);

    _state.last_seen_ms = esp_timer_get_time() / 1000;
    if (_callback) _callback(_state);
}

void BambuClient::parse_print(cJSON* p) {
    // Status
    const char* st = json_str(p, "gcode_state");
    if      (strcmp(st, "IDLE")    == 0) _state.status = PrinterStatus::IDLE;
    else if (strcmp(st, "RUNNING") == 0) _state.status = PrinterStatus::PRINTING;
    else if (strcmp(st, "PAUSE")   == 0) _state.status = PrinterStatus::PAUSED;
    else if (strcmp(st, "FINISH")  == 0) _state.status = PrinterStatus::FINISHED;
    else if (strcmp(st, "FAILED")  == 0) _state.status = PrinterStatus::FAILED;

    // Progress & ETA
    _state.progress_pct   = json_int(p, "mc_percent");
    _state.remaining_sec  = json_int(p, "mc_remaining_time") * 60; // Bambu sends minutes
    _state.layer_cur      = json_int(p, "layer_num");
    _state.layer_total    = json_int(p, "total_layer_num");
    _state.subtask_name   = json_str(p, "subtask_name");
    _state.gcode_file     = json_str(p, "gcode_file");

    // Temperatures
    _state.nozzle_temp   = json_float(p, "nozzle_temper");
    _state.nozzle_target = json_float(p, "nozzle_target_temper");
    _state.bed_temp      = json_float(p, "bed_temper");
    _state.bed_target    = json_float(p, "bed_target_temper");
    _state.chamber_temp  = json_float(p, "chamber_temper");

    // Fans (Bambu reports 0-15, scale to %)
    auto fan_pct = [](int v) { return (v * 100) / 15; };
    _state.fan_part_cooling = fan_pct(json_int(p, "cooling_fan_speed"));
    _state.fan_aux          = fan_pct(json_int(p, "big_fan1_speed"));
    _state.fan_chamber      = fan_pct(json_int(p, "big_fan2_speed"));

    // Speed
    _state.print_speed_pct = json_int(p, "spd_lvl", 1) == 1 ? 50  :
                             json_int(p, "spd_lvl", 2) == 2 ? 100 :
                             json_int(p, "spd_lvl", 3) == 3 ? 124 : 166;
    // Lights
    cJSON* lights = cJSON_GetObjectItem(p, "lights_report");
    if (lights && cJSON_IsArray(lights)) {
        cJSON* l;
        cJSON_ArrayForEach(l, lights) {
            const char* node = json_str(l, "node");
            bool on = strcmp(json_str(l, "mode"), "on") == 0;
            if (strcmp(node, "chamber_light") == 0) _state.chamber_light = on;
            if (strcmp(node, "work_light")    == 0) _state.work_light    = on;
        }
    }
}

void BambuClient::parse_ams(cJSON* ams_obj) {
    cJSON* ams_arr = cJSON_GetObjectItem(ams_obj, "ams");
    if (!ams_arr || !cJSON_IsArray(ams_arr)) return;

    _state.ams_trays.clear();

    cJSON* tray_json;
    cJSON_ArrayForEach(tray_json, ams_arr) {
        AmsTray tray;
        tray.id       = json_int(tray_json, "id");
        tray.humidity = json_int(tray_json, "humidity");
        tray.temp     = json_int(tray_json, "temp");

        cJSON* slots = cJSON_GetObjectItem(tray_json, "tray");
        if (slots && cJSON_IsArray(slots)) {
            cJSON* slot_json;
            cJSON_ArrayForEach(slot_json, slots) {
                AmsSlot slot;
                slot.id         = json_int(slot_json, "id");
                slot.color      = json_str(slot_json, "tray_color");
                slot.material   = json_str(slot_json, "tray_type");
                slot.remain_pct = json_int(slot_json, "remain", -1);
                tray.slots.push_back(slot);
            }
        }
        _state.ams_trays.push_back(tray);
    }
}

// ── Commands ──────────────────────────────────────────────────────────────────

esp_err_t BambuClient::publish(const char* json) {
    if (!_connected || !_mqtt_handle) return ESP_ERR_INVALID_STATE;
    int id = esp_mqtt_client_publish(
        (esp_mqtt_client_handle_t)_mqtt_handle,
        _request_topic.c_str(), json, 0, 0, 0);
    return id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t BambuClient::set_light(bool chamber, bool on) {
    const char* node = chamber ? "chamber_light" : "work_light";
    const char* mode = on      ? "on"            : "off";
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"system\":{\"sequence_id\":\"0\",\"command\":\"ledctrl\","
        "\"led_node\":\"%s\",\"led_mode\":\"%s\","
        "\"led_on_time\":500,\"led_off_time\":500,\"loop_times\":0,\"interval_time\":0}}",
        node, mode);
    return publish(buf);
}

esp_err_t BambuClient::set_speed(int pct) {
    int lvl = pct <= 50 ? 1 : pct <= 100 ? 2 : pct <= 124 ? 3 : 4;
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"print\":{\"sequence_id\":\"0\",\"command\":\"print_speed\",\"param\":\"%d\"}}",
        lvl);
    return publish(buf);
}

esp_err_t BambuClient::pause_print() {
    return publish("{\"print\":{\"sequence_id\":\"0\",\"command\":\"pause\"}}");
}

esp_err_t BambuClient::resume_print() {
    return publish("{\"print\":{\"sequence_id\":\"0\",\"command\":\"resume\"}}");
}

esp_err_t BambuClient::stop_print() {
    return publish("{\"print\":{\"sequence_id\":\"0\",\"command\":\"stop\"}}");
}

esp_err_t BambuClient::set_fan(const char* fan, int speed) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"print\":{\"sequence_id\":\"0\",\"command\":\"print_option\","
        "\"%s\":%d}}", fan, speed);
    return publish(buf);
}

esp_err_t BambuClient::set_temperature(const char* target, float temp) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"print\":{\"sequence_id\":\"0\",\"command\":\"gcode_line\","
        "\"param\":\"M104 S%.0f\\n\"}}", temp); // simplified; bed uses M140
    return publish(buf);
}

PrinterState BambuClient::state() const {
    return _state;
}
