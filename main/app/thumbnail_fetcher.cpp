#include "thumbnail_fetcher.hpp"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char* TAG = "thumb";

ThumbnailFetcher& ThumbnailFetcher::instance() {
    static ThumbnailFetcher inst;
    return inst;
}

// Bambu printers expose a minimal HTTPS server on port 443 in LAN mode
// that serves thumbnail PNGs at a known path.
std::vector<uint8_t> ThumbnailFetcher::do_http_fetch(
    const std::string& ip,
    const std::string& access_code,
    const std::string& subtask)
{
    // URL: https://<ip>/print/thumbnail?taskid=<subtask>
    // Auth: Basic bblp:<access_code>
    std::string url = "https://" + ip + "/print/thumbnail?taskid=" + subtask;

    std::vector<uint8_t> buf;

    esp_http_client_config_t cfg = {};
    cfg.url                = url.c_str();
    cfg.username           = "bblp";
    cfg.password           = access_code.c_str();
    cfg.auth_type          = HTTP_AUTH_TYPE_BASIC;
    cfg.skip_cert_common_name_check = true;
    cfg.crt_bundle_attach  = nullptr;
    cfg.transport_type     = HTTP_TRANSPORT_OVER_SSL;
    cfg.timeout_ms         = 5000;
    cfg.buffer_size        = 4096;

    auto write_cb = [](esp_http_client_event_t* evt) -> esp_err_t {
        if (evt->event_id == HTTP_EVENT_ON_DATA) {
            auto* v = static_cast<std::vector<uint8_t>*>(evt->user_data);
            const uint8_t* d = static_cast<uint8_t*>(evt->data);
            v->insert(v->end(), d, d + evt->data_len);
        }
        return ESP_OK;
    };
    cfg.event_handler = write_cb;
    cfg.user_data     = &buf;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "Thumbnail fetch failed: err=%d status=%d", err, status);
        return {};
    }

    ESP_LOGI(TAG, "Thumbnail: %zu bytes", buf.size());
    return buf;
}

void ThumbnailFetcher::fetch_task(void* arg) {
    auto* a = static_cast<FetchArgs*>(arg);
    auto data = do_http_fetch(a->ip, a->access_code, a->subtask_name);
    if (a->cb) a->cb(data);
    delete a;
    vTaskDelete(nullptr);
}

void ThumbnailFetcher::fetch(const std::string& ip,
                              const std::string& access_code,
                              const std::string& subtask_name,
                              ThumbnailCallback  cb) {
    auto* args = new FetchArgs{ip, access_code, subtask_name, cb};
    xTaskCreate(fetch_task, "thumb", 8192, args, 3, nullptr);
}
