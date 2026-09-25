#ifndef DIAGNOSTIC_LOG_H
#define DIAGNOSTIC_LOG_H

#include "esp_log.h"

/*
 * Versioned markers are consumed by blackbox_service's ESP_LOG hook.
 * Components only depend on the logging contract, not the persistence backend.
 */
#define DIAGNOSTIC_LOG_TEXT_MARKER "@DLOG1:T@ "
#define DIAGNOSTIC_LOG_SNAPSHOT_MARKER "@DLOG1:S@ "

#define DEVICE_EVENT_I(tag, fmt, ...) ESP_LOGI(tag, DIAGNOSTIC_LOG_TEXT_MARKER fmt, ##__VA_ARGS__)
#define DEVICE_STATE_I(tag, fmt, ...) ESP_LOGI(tag, DIAGNOSTIC_LOG_SNAPSHOT_MARKER fmt, ##__VA_ARGS__)

#define DEVICE_EVENT_W(tag, fmt, ...) ESP_LOGW(tag, DIAGNOSTIC_LOG_TEXT_MARKER fmt, ##__VA_ARGS__)
#define DEVICE_STATE_W(tag, fmt, ...) ESP_LOGW(tag, DIAGNOSTIC_LOG_SNAPSHOT_MARKER fmt, ##__VA_ARGS__)

#define DEVICE_EVENT_E(tag, fmt, ...) ESP_LOGE(tag, DIAGNOSTIC_LOG_TEXT_MARKER fmt, ##__VA_ARGS__)
#define DEVICE_STATE_E(tag, fmt, ...) ESP_LOGE(tag, DIAGNOSTIC_LOG_SNAPSHOT_MARKER fmt, ##__VA_ARGS__)

#endif
