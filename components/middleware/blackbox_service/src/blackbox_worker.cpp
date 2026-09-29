#include "blackbox_service_internal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace BlackboxService::Internal {
namespace {

constexpr TickType_t WORKER_POLL_TICKS = pdMS_TO_TICKS(20);
SemaphoreHandle_t drain_mutex;
uint32_t persist_failures;

esp_err_t drain_pending_logs_locked() {
    // Flash 写入只在工作任务中执行，日志 Hook 只负责将定长事件压入 RAM 环形队列。
    esp_err_t result = ESP_OK;
    LogEvent event = {};
    while (pop_log_event(&event)) {
        const esp_err_t append_result = Blackbox::append_text("%s", event.text);
        if (result == ESP_OK && append_result != ESP_OK) {
            result = append_result;
        }
        if (append_result != ESP_OK) {
            ++persist_failures;
        }
    }
    return result;
}

void blackbox_service_task(void*) {
    while (true) {
        drain_pending_logs();
        vTaskDelay(WORKER_POLL_TICKS);
    }
}

} // namespace

esp_err_t drain_pending_logs() {
    if (drain_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(drain_mutex, portMAX_DELAY);
    const esp_err_t result = drain_pending_logs_locked();
    xSemaphoreGive(drain_mutex);
    return result;
}

esp_err_t sync_pending_logs() {
    if (drain_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(drain_mutex, portMAX_DELAY);
    const esp_err_t drain_result = drain_pending_logs_locked();
    const esp_err_t sync_result = Blackbox::sync();
    xSemaphoreGive(drain_mutex);
    return drain_result != ESP_OK ? drain_result : sync_result;
}

esp_err_t start_worker() {
    drain_mutex = xSemaphoreCreateMutex();
    if (drain_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t result =
        xTaskCreate(blackbox_service_task, "bb_service_t", 3072, nullptr, 3, nullptr);
    if (result != pdPASS) {
        vSemaphoreDelete(drain_mutex);
        drain_mutex = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

uint32_t get_persist_failures() {
    if (drain_mutex == nullptr) {
        return 0;
    }
    xSemaphoreTake(drain_mutex, portMAX_DELAY);
    const uint32_t result = persist_failures;
    xSemaphoreGive(drain_mutex);
    return result;
}

} // namespace BlackboxService::Internal
