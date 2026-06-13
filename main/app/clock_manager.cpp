#include "clock_manager.hpp"
#include "esp_sntp.h"
#include "esp_log.h"
#include <time.h>
#include <cstdio>

static const char* TAG = "clock";

ClockManager& ClockManager::instance() {
    static ClockManager inst;
    return inst;
}

void ClockManager::sntp_sync_cb(struct timeval*) {
    ClockManager::instance()._synced = true;
    ESP_LOGI(TAG, "SNTP synced");
}

esp_err_t ClockManager::init(const char* ntp_server, const char* timezone) {
    setenv("TZ", timezone, 1);
    tzset();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, ntp_server);
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
    sntp_init();

    ESP_LOGI(TAG, "SNTP started, tz=%s", timezone);
    return ESP_OK;
}

std::string ClockManager::time_str() const {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return buf;
}

std::string ClockManager::date_str() const {
    time_t now;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    char buf[20];
    static const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                   "Jul","Aug","Sep","Oct","Nov","Dec"};
    snprintf(buf, sizeof(buf), "%s %d %s",
             days[t.tm_wday], t.tm_mday, months[t.tm_mon]);
    return buf;
}
