#pragma once
#include <string>
#include <vector>
#include <functional>
#include "esp_err.h"

// Fixes issue #229: thumbnail preview in LAN mode
// Bambu printers expose thumbnails via FTP over implicit TLS (port 990).
// Credentials: username "bblp", password = printer access code.
// Path (community-documented, unverified on hardware):
//   /cache/<subtask_name>/thumbnail/plate_1.png

using ThumbnailCallback = std::function<void(const std::vector<uint8_t>& png)>;

class ThumbnailFetcher {
public:
    static ThumbnailFetcher& instance();

    // Async fetch — calls cb on main task when done (or empty vector on fail)
    void fetch(const std::string& printer_ip,
               const std::string& access_code,
               const std::string& subtask_name,
               ThumbnailCallback  cb);

private:
    ThumbnailFetcher() = default;

    struct FetchArgs {
        std::string        ip;
        std::string        access_code;
        std::string        subtask_name;
        ThumbnailCallback  cb;
    };

    static void fetch_task(void* arg);
    static std::vector<uint8_t> do_ftps_fetch(const std::string& ip,
                                               const std::string& access_code,
                                               const std::string& subtask);
};
