#ifndef BLACKBOX_SERVICE_INTERNAL_H
#define BLACKBOX_SERVICE_INTERNAL_H

#include "blackbox.h"
#include "blackbox_service.h"
#include "esp_err.h"

namespace BlackboxService::Internal {

struct LogEvent {
    char text[Blackbox::TEXT_BUFFER_SIZE];
};

void install_log_capture(const Config& config);
bool pop_log_event(LogEvent* event);
void get_capture_statistics(uint32_t* captured_logs,
                            uint32_t* dropped_logs,
                            size_t* pending_logs);
uint32_t get_persist_failures();
esp_err_t drain_pending_logs();
esp_err_t sync_pending_logs();
esp_err_t start_worker();

} // namespace BlackboxService::Internal

#endif
