/*
 * @Description: 基于 esp_netif_sntp 的中间件时间同步服务实现
 */
#include "time_service.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "diagnostic_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace TimeService {
namespace {

constexpr char     TAG[]              = "TimeService";
constexpr char     TIMEZONE[]         = "CST-8";
constexpr size_t   NTP_SERVER_COUNT   = 5;
constexpr size_t   EVENT_QUEUE_LENGTH = 8;
constexpr uint32_t WORKER_STACK_SIZE  = 3072;

// 事件回调只投递轻量消息，格式化日志和 SNTP 重启均在后台任务执行。
enum class EventType : uint8_t {
    SYNCED,
    NETWORK_READY,
};

struct Event {
    EventType      type;
    struct timeval synced_time;
};

bool          initialized  = false;
bool          synchronized = false;
time_t        last_sync    = 0;
QueueHandle_t event_queue  = nullptr;
TaskHandle_t  worker_task  = nullptr;
portMUX_TYPE  state_lock   = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief 非阻塞投递时间服务事件
 *
 * 回调运行在系统事件循环中，队列满时直接丢弃，避免阻塞网络事件处理。
 * 队列溢出表示时间状态路径已丢失，使用 WARN 进入黑匣子。
 */
void enqueue_event(const Event& event) {
    if (event_queue == nullptr || xQueueSend(event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "time: event_queue result=dropped type=%u", static_cast<uint32_t>(event.type));
    }
}

void sntp_event_handler(void*, esp_event_base_t, int32_t event_id, void* event_data) {
    if (event_id != NETIF_SNTP_TIME_SYNC || event_data == nullptr) {
        return;
    }

    const auto* sync_event = static_cast<const esp_netif_sntp_time_sync_t*>(event_data);
    Event       event      = {};
    event.type             = EventType::SYNCED;
    event.synced_time      = sync_event->tv;
    enqueue_event(event);
}

void ip_event_handler(void*, esp_event_base_t, int32_t event_id, void*) {
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }

    Event event = {};
    event.type  = EventType::NETWORK_READY;
    enqueue_event(event);
}

/**
 * @brief 更新同步状态并输出便于黑匣子归档的时间日志
 *
 * 三条 WARN 使用相同 unix_s 作为关联键，CSV 导出后可对齐原始时间戳、
 * UTC+0 和 UTC+8。拆分日志可缩短单条文本，减少底层 STRING 分片数量。
 */
void log_sync_event(const struct timeval& synced_time) {
    const time_t utc_seconds = synced_time.tv_sec;
    struct tm    utc         = {};
    struct tm    local       = {};
    gmtime_r(&utc_seconds, &utc);
    localtime_r(&utc_seconds, &local);

    char utc_text[24]   = {};
    char local_text[30] = {};
    strftime(utc_text, sizeof(utc_text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    strftime(local_text, sizeof(local_text), "%Y-%m-%dT%H:%M:%S+08:00", &local);

    portENTER_CRITICAL(&state_lock);
    synchronized = true;
    last_sync    = utc_seconds;
    portEXIT_CRITICAL(&state_lock);

    DEVICE_STATE_I(TAG, "time: sync old=unsynchronized new=synchronized unix_s=%lld unix_us=%ld",
                   static_cast<int64_t>(utc_seconds), static_cast<int32_t>(synced_time.tv_usec));
    DEVICE_EVENT_I(TAG, "time: utc unix_s=%lld iso=%s", static_cast<int64_t>(utc_seconds), utc_text);
    DEVICE_EVENT_I(TAG, "time: local unix_s=%lld iso=%s timezone=%s", static_cast<int64_t>(utc_seconds), local_text,
                   TIMEZONE);
}

void time_service_task(void*) {
    Event event = {};
    while (true) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (event.type == EventType::SYNCED) {
            log_sync_event(event.synced_time);
            continue;
        }

        // STA 恢复 IP 后主动重启请求，不等待下一次 lwIP 周期校时。
        const esp_err_t ret = esp_netif_sntp_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "time: sntp_restart source=network_recovery result=%s", esp_err_to_name(ret));
        } else {
            DEVICE_EVENT_I(TAG, "time: sntp_restart source=network_recovery result=ok");
        }
    }
}

} // namespace

esp_err_t init() {
    if (initialized) {
        return ESP_OK;
    }

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    if (setenv("TZ", TIMEZONE, 1) != 0) {
        return ESP_FAIL;
    }
    tzset();

    event_queue = xQueueCreate(EVENT_QUEUE_LENGTH, sizeof(Event));
    if (event_queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(time_service_task, "time_service", WORKER_STACK_SIZE, nullptr, 3, &worker_task) != pdPASS) {
        vQueueDelete(event_queue);
        event_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    ret = esp_event_handler_register(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC, sntp_event_handler, nullptr);
    if (ret != ESP_OK) {
        vTaskDelete(worker_task);
        vQueueDelete(event_queue);
        worker_task = nullptr;
        event_queue = nullptr;
        return ret;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, nullptr);
    if (ret != ESP_OK) {
        esp_event_handler_unregister(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC, sntp_event_handler);
        vTaskDelete(worker_task);
        vQueueDelete(event_queue);
        worker_task = nullptr;
        event_queue = nullptr;
        return ret;
    }

    // 仅组合标准 UTC 时间源，避免混用 leap-smear 与非 leap-smear 服务。
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        NTP_SERVER_COUNT, ESP_SNTP_SERVER_LIST("0.pool.ntp.org", "time.cloudflare.com", "time.nist.gov",
                                               "1.pool.ntp.org", "2.pool.ntp.org"));
    config.smooth_sync   = true;
    config.wait_for_sync = false;
    ret                  = esp_netif_sntp_init(&config);
    if (ret != ESP_OK) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler);
        esp_event_handler_unregister(NETIF_SNTP_EVENT, NETIF_SNTP_TIME_SYNC, sntp_event_handler);
        vTaskDelete(worker_task);
        vQueueDelete(event_queue);
        worker_task = nullptr;
        event_queue = nullptr;
        return ret;
    }

    initialized = true;
    DEVICE_EVENT_I(TAG, "time: init timezone=%s ntp_servers=%u smooth_sync=1 result=ok", TIMEZONE,
                   static_cast<uint32_t>(NTP_SERVER_COUNT));
    return ESP_OK;
}

bool is_synchronized() {
    portENTER_CRITICAL(&state_lock);
    const bool result = synchronized;
    portEXIT_CRITICAL(&state_lock);
    return result;
}

time_t now_utc() {
    time_t now = 0;
    time(&now);
    return now;
}

bool get_local_time(struct tm* out) {
    if (out == nullptr || !is_synchronized()) {
        return false;
    }

    const time_t now = now_utc();
    return localtime_r(&now, out) != nullptr;
}

time_t last_sync_utc() {
    portENTER_CRITICAL(&state_lock);
    const time_t result = last_sync;
    portEXIT_CRITICAL(&state_lock);
    return result;
}

} // namespace TimeService
