#include "thumbnail_fetcher.hpp"
#include "esp_tls.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

static const char* TAG = "thumb";

ThumbnailFetcher& ThumbnailFetcher::instance() {
    static ThumbnailFetcher inst;
    return inst;
}

// ── FTPS helpers ──────────────────────────────────────────────────────────────

static bool tls_readline(esp_tls_t* tls, char* buf, size_t size) {
    size_t n = 0;
    while (n < size - 1) {
        char c;
        if (esp_tls_conn_read(tls, &c, 1) != 1) return false;
        if (c == '\n') break;
        buf[n++] = c;
    }
    // Strip trailing CR
    if (n > 0 && buf[n - 1] == '\r') n--;
    buf[n] = '\0';
    return true;
}

// Drains FTP multi-line replies ("XXX-..." until "XXX "), returns numeric code.
static int ftp_reply(esp_tls_t* tls, char* buf, size_t size) {
    if (!tls_readline(tls, buf, size)) return -1;
    int code = atoi(buf);
    while (strlen(buf) > 3 && buf[3] == '-') {
        if (!tls_readline(tls, buf, size)) return -1;
    }
    return code;
}

static bool ftp_cmd(esp_tls_t* tls, const char* cmd) {
    ssize_t len = (ssize_t)strlen(cmd);
    return esp_tls_conn_write(tls, cmd, len) == len;
}

// ── FTPS thumbnail fetch (issue #229) ────────────────────────────────────────
//
// Protocol: FTP over implicit TLS, port 990, passive mode.
// Auth: USER bblp / PASS <access_code> (same credential as MQTT).
// Thumbnail path (community-documented, path unverified on hardware):
//   /cache/<subtask_name>/thumbnail/plate_1.png
//
// CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y (in sdkconfig.defaults) lets us
// connect to the printer's self-signed certificate without a CA bundle.

std::vector<uint8_t> ThumbnailFetcher::do_ftps_fetch(
    const std::string& ip,
    const std::string& access_code,
    const std::string& subtask)
{
    char buf[512], cmd[400];

    esp_tls_cfg_t tls_cfg = {};
    tls_cfg.skip_common_name = true;

    // ── Control channel (implicit TLS on port 990) ────────────────────────────
    esp_tls_t* ctrl = esp_tls_init();
    if (!ctrl) return {};

    // RAII: destroy control channel on any return path
    struct TlsGuard {
        esp_tls_t* tls;
        ~TlsGuard() { if (tls) esp_tls_conn_destroy(tls); }
    } ctrl_guard{ctrl};

    if (esp_tls_conn_new_sync(ip.c_str(), ip.size(), 990, &tls_cfg, ctrl) != 1) {
        ESP_LOGW(TAG, "FTPS: cannot connect %s:990", ip.c_str());
        return {};
    }

    if (ftp_reply(ctrl, buf, sizeof(buf)) != 220) return {};

    ftp_cmd(ctrl, "USER bblp\r\n");
    if (ftp_reply(ctrl, buf, sizeof(buf)) != 331) return {};

    snprintf(cmd, sizeof(cmd), "PASS %s\r\n", access_code.c_str());
    ftp_cmd(ctrl, cmd);
    if (ftp_reply(ctrl, buf, sizeof(buf)) != 230) {
        ESP_LOGW(TAG, "FTPS: auth failed");
        return {};
    }

    ftp_cmd(ctrl, "TYPE I\r\n");
    ftp_reply(ctrl, buf, sizeof(buf));

    // PASV — passive mode: server tells us where to connect for data
    ftp_cmd(ctrl, "PASV\r\n");
    if (ftp_reply(ctrl, buf, sizeof(buf)) != 227) return {};

    int h1 = 0, h2 = 0, h3 = 0, h4 = 0, p1 = 0, p2 = 0;
    char* paren = strchr(buf, '(');
    if (!paren || sscanf(paren, "(%d,%d,%d,%d,%d,%d)",
                         &h1, &h2, &h3, &h4, &p1, &p2) != 6)
        return {};

    int  data_port = p1 * 256 + p2;
    char data_ip[24];
    snprintf(data_ip, sizeof(data_ip), "%d.%d.%d.%d", h1, h2, h3, h4);

    // ── Data channel (connect before RETR so server can start streaming) ──────
    esp_tls_t* data = esp_tls_init();
    if (!data) return {};

    TlsGuard data_guard{data};

    if (esp_tls_conn_new_sync(data_ip, strlen(data_ip), data_port, &tls_cfg, data) != 1) {
        ESP_LOGW(TAG, "FTPS: data connect failed");
        return {};
    }

    // Request the file on the control channel
    snprintf(cmd, sizeof(cmd),
             "RETR /cache/%s/thumbnail/plate_1.png\r\n", subtask.c_str());
    ftp_cmd(ctrl, cmd);

    // 125 = already open, 150 = about to open
    int code = ftp_reply(ctrl, buf, sizeof(buf));
    if (code != 125 && code != 150) {
        ESP_LOGW(TAG, "FTPS: RETR %d — %s", code, buf);
        return {};
    }

    // Stream PNG from data channel
    std::vector<uint8_t> result;
    result.reserve(16 * 1024);
    uint8_t chunk[2048];
    ssize_t n;
    while ((n = esp_tls_conn_read(data, chunk, sizeof(chunk))) > 0)
        result.insert(result.end(), chunk, chunk + n);

    // 226 Transfer Complete (best-effort read, don't fail on missing)
    ftp_reply(ctrl, buf, sizeof(buf));
    ftp_cmd(ctrl, "QUIT\r\n");

    ESP_LOGI(TAG, "Thumbnail: %zu bytes via FTPS", result.size());
    return result;
}

// ── Async wrapper ─────────────────────────────────────────────────────────────

void ThumbnailFetcher::fetch_task(void* arg) {
    auto* a = static_cast<FetchArgs*>(arg);
    auto data = do_ftps_fetch(a->ip, a->access_code, a->subtask_name);
    if (a->cb) a->cb(data);
    delete a;
    vTaskDelete(nullptr);
}

void ThumbnailFetcher::fetch(const std::string& ip,
                              const std::string& access_code,
                              const std::string& subtask_name,
                              ThumbnailCallback  cb) {
    auto* args = new FetchArgs{ip, access_code, subtask_name, cb};
    // FIX H4: check task creation — free args and signal failure on OOM
    if (xTaskCreate(fetch_task, "thumb", 8192, args, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create thumbnail task");
        if (args->cb) args->cb({});
        delete args;
    }
}
