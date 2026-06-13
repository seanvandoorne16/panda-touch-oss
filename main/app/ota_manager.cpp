#include "ota_manager.hpp"
#include "version.hpp"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ota";

OtaManager& OtaManager::instance() {
    static OtaManager inst;
    return inst;
}

const char* OtaManager::current_version() {
    return FIRMWARE_VERSION;
}

// ── OTA task ─────────────────────────────────────────────────────────────────

void OtaManager::ota_task(void* arg) {
    auto* a = static_cast<UpdateArgs*>(arg);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url                         = a->url;
    http_cfg.skip_cert_common_name_check = true;
    http_cfg.timeout_ms                  = 10000;
    http_cfg.buffer_size                 = 4096;
    http_cfg.buffer_size_tx              = 1024;
    http_cfg.keep_alive_enable           = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    esp_https_ota_handle_t handle = nullptr;
    esp_err_t err = esp_https_ota_begin(&ota_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        if (a->done_cb) a->done_cb(false, "Verbinding mislukt");
        a->self->_updating = false;
        delete a;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "OTA download gestart");

    while (true) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;

        int total   = esp_https_ota_get_image_size(handle);
        int written = esp_https_ota_get_image_len_read(handle);
        if (total > 0 && a->progress_cb) {
            a->progress_cb((written * 100) / total);
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA perform failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(handle);
        if (a->done_cb) a->done_cb(false, "Download mislukt");
        a->self->_updating = false;
        delete a;
        vTaskDelete(nullptr);
        return;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish failed: %s", esp_err_to_name(err));
        if (a->done_cb) a->done_cb(false, "Flash mislukt");
        a->self->_updating = false;
        delete a;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "OTA geslaagd — opnieuw opstarten");
    if (a->done_cb) a->done_cb(true, "Bijgewerkt! Opnieuw opstarten...");
    delete a;

    vTaskDelay(pdMS_TO_TICKS(2000));  // Even wachten zodat UI de boodschap toont
    esp_restart();

    vTaskDelete(nullptr);
}

// ── Public API ────────────────────────────────────────────────────────────────

void OtaManager::start_update(const char* url,
                               OtaProgressCallback progress_cb,
                               OtaDoneCallback     done_cb) {
    if (_updating) {
        ESP_LOGW(TAG, "OTA al bezig");
        return;
    }
    _updating = true;

    auto* args = new UpdateArgs{url, progress_cb, done_cb, this};

    if (xTaskCreate(ota_task, "ota", 8192, args, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Kan OTA-taak niet aanmaken");
        if (done_cb) done_cb(false, "Niet genoeg geheugen");
        _updating = false;
        delete args;
    }
}
