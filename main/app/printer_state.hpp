#pragma once
#include <string>
#include <vector>
#include <cstdint>

enum class PrinterStatus {
    UNKNOWN,
    IDLE,
    PRINTING,
    PAUSED,
    FINISHED,
    FAILED,
    OFFLINE,
};

struct AmsSlot {
    int     id;
    std::string color;   // hex e.g. "FF0000"
    std::string material;
    int     remain_pct;  // 0-100, -1 = unknown
};

struct AmsTray {
    int                  id;
    std::vector<AmsSlot> slots;
    int                  humidity;   // 1-5
    int                  temp;       // celsius
};

struct PrinterState {
    // Identity
    std::string serial;
    std::string name;
    std::string ip;
    std::string access_code;

    // Connectivity
    bool        online       = false;
    int64_t     last_seen_ms = 0;

    // Print job
    PrinterStatus status     = PrinterStatus::UNKNOWN;
    std::string   gcode_file;
    int           progress_pct = 0;   // 0-100
    int           remaining_sec = 0;  // seconds left, 0 = unknown
    int           layer_cur  = 0;
    int           layer_total = 0;
    std::string   subtask_name;

    // Temperatures
    float nozzle_temp      = 0.f;
    float nozzle_target    = 0.f;
    float bed_temp         = 0.f;
    float bed_target       = 0.f;
    float chamber_temp     = 0.f;

    // Fans (%)
    int   fan_part_cooling = 0;
    int   fan_aux          = 0;
    int   fan_chamber      = 0;

    // Speeds
    int   print_speed_pct  = 100;  // 100 = normal

    // Lights
    bool  chamber_light    = false;
    bool  work_light       = false;

    // AMS
    std::vector<AmsTray> ams_trays;

    // Thumbnail (JPEG bytes, empty if none)
    std::vector<uint8_t> thumbnail;

    // Helpers
    const char* status_str() const {
        switch (status) {
            case PrinterStatus::IDLE:     return "Idle";
            case PrinterStatus::PRINTING: return "Printing";
            case PrinterStatus::PAUSED:   return "Paused";
            case PrinterStatus::FINISHED: return "Finished";
            case PrinterStatus::FAILED:   return "Failed";
            case PrinterStatus::OFFLINE:  return "Offline";
            default:                      return "Unknown";
        }
    }

    // Format remaining time as "1h 23m" or "45m" or "< 1m"
    std::string remaining_str() const {
        if (remaining_sec <= 0) return "--";
        int h = remaining_sec / 3600;
        int m = (remaining_sec % 3600) / 60;
        if (h > 0) {
            return std::to_string(h) + "h " + std::to_string(m) + "m";
        }
        if (m > 0) return std::to_string(m) + "m";
        return "< 1m";
    }
};
