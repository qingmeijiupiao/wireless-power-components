#include "blackbox_service.h"

#include <cstdarg>
#include <cstdio>

#include "blackbox.h"
#include "blackbox_service_internal.h"

namespace BlackboxService {
namespace {

bool initialized;

esp_err_t append_v(const char* fmt, va_list args) {
    if (fmt == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    char text[Blackbox::TEXT_BUFFER_SIZE] = {};
    vsnprintf(text, sizeof(text), fmt, args);
    return Blackbox::append_text("%s", text);
}

} // namespace

esp_err_t init(const Config& config) {
    if (initialized) {
        return ESP_OK;
    }
    if (config.info_tag_count && !config.info_tags) return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < config.info_tag_count; ++i) {
        if (!config.info_tags[i] || !config.info_tags[i][0]) return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t worker_result = Internal::start_worker();
    if (worker_result != ESP_OK) {
        return worker_result;
    }
    Internal::install_log_capture(config);
    initialized = true;
    return Blackbox::append_text("blackbox_service: initialized");
}

esp_err_t sync() {
    return Internal::sync_pending_logs();
}

esp_err_t append_event(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const esp_err_t ret = append_v(fmt, args);
    va_end(args);
    return ret;
}

esp_err_t append_text_event(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const esp_err_t ret = append_v(fmt, args);
    va_end(args);
    return ret;
}

void get_statistics(Statistics* statistics) {
    if (statistics == nullptr) {
        return;
    }
    Internal::get_capture_statistics(&statistics->captured_logs,
                                     &statistics->dropped_logs,
                                     &statistics->pending_logs);
    statistics->persist_failures = Internal::get_persist_failures();
}

} // namespace BlackboxService
