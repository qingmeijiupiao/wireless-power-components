#include "blackbox_service_internal.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log_write.h"
#include "freertos/FreeRTOS.h"

namespace BlackboxService::Internal {
namespace {

constexpr size_t LOG_LINE_BUFFER_SIZE = 256;
constexpr size_t LOG_EVENT_RING_SIZE = 64;

portMUX_TYPE event_ring_lock = portMUX_INITIALIZER_UNLOCKED;
LogEvent event_ring[LOG_EVENT_RING_SIZE];
size_t event_ring_read;
size_t event_ring_write;
size_t event_ring_used;
uint32_t captured_logs;
uint32_t dropped_logs;
vprintf_like_t previous_log_vprintf;
bool capture_busy;
char capture_line[LOG_LINE_BUFFER_SIZE];
char clean_line[LOG_LINE_BUFFER_SIZE];
LogEvent capture_event;
Config capture_config;

bool is_internal_tag(const char* tag, size_t length) {
    // 过滤黑匣子自身日志，防止“写日志产生新日志”的递归捕获。
    constexpr const char* INTERNAL_TAGS[] = {
        "Blackbox",
        "BlackBox",
        "CircularFlashBuffer",
    };
    for (const char* internal_tag : INTERNAL_TAGS) {
        if (strlen(internal_tag) == length &&
            strncmp(tag, internal_tag, length) == 0) {
            return true;
        }
    }
    return false;
}

bool keep_info_tag(const char* tag, size_t length) {
    for (size_t i = 0; i < capture_config.info_tag_count; ++i) {
        const char* info_tag = capture_config.info_tags[i];
        if (strlen(info_tag) == length &&
            strncmp(tag, info_tag, length) == 0) {
            return true;
        }
    }
    return false;
}

void strip_ansi(const char* input, char* output, size_t output_size) {
    size_t output_index = 0;
    for (size_t input_index = 0;
         input[input_index] != '\0' && output_index + 1 < output_size;) {
        if (input[input_index] == '\x1b' && input[input_index + 1] == '[') {
            input_index += 2;
            while (input[input_index] != '\0' &&
                   (input[input_index] < '@' || input[input_index] > '~')) {
                ++input_index;
            }
            if (input[input_index] != '\0') {
                ++input_index;
            }
            continue;
        }
        output[output_index++] = input[input_index++];
    }
    output[output_index] = '\0';
}

bool format_log_event(const char* raw, LogEvent* event) {
    strip_ansi(raw, clean_line, sizeof(clean_line));

    const char level = clean_line[0];
    if (level != 'I' && level != 'W' && level != 'E') {
        return false;
    }

    const char* tag = strstr(clean_line, ") ");
    if (tag == nullptr) {
        return false;
    }
    tag += 2;

    const char* message = strstr(tag, ": ");
    if (message == nullptr || message == tag) {
        return false;
    }
    const size_t tag_length = static_cast<size_t>(message - tag);
    if (is_internal_tag(tag, tag_length)) {
        return false;
    }
    if (level == 'I' && !keep_info_tag(tag, tag_length)) {
        return false;
    }
    message += 2;

    size_t message_length = strlen(message);
    while (message_length > 0 &&
           (message[message_length - 1] == '\r' ||
            message[message_length - 1] == '\n')) {
        --message_length;
    }

    snprintf(event->text,
             sizeof(event->text),
             "[%c][%.*s] %.*s",
             level,
             static_cast<int>(tag_length),
             tag,
             static_cast<int>(message_length),
             message);
    return true;
}

void push_log_event(const LogEvent& event) {
    portENTER_CRITICAL(&event_ring_lock);
    if (event_ring_used < LOG_EVENT_RING_SIZE) {
        event_ring[event_ring_write] = event;
        event_ring_write = (event_ring_write + 1) % LOG_EVENT_RING_SIZE;
        ++event_ring_used;
        ++captured_logs;
    } else {
        ++dropped_logs;
    }
    portEXIT_CRITICAL(&event_ring_lock);
}

int blackbox_log_vprintf(const char* format, va_list args) {
    int output_length = 0;
    if (previous_log_vprintf != nullptr) {
        va_list output_args;
        va_copy(output_args, args);
        output_length = previous_log_vprintf(format, output_args);
        va_end(output_args);
    }

    // ESP 日志 Hook 可能被不同任务并发调用。捕获区使用固定静态缓冲，重入时
    // 保留原串口输出但跳过持久化，避免破坏正在格式化的记录。
    if (__atomic_test_and_set(&capture_busy, __ATOMIC_ACQUIRE)) {
        return output_length;
    }

    va_list capture_args;
    va_copy(capture_args, args);
    const int captured_length =
        vsnprintf(capture_line, sizeof(capture_line), format, capture_args);
    va_end(capture_args);

    if (captured_length > 0) {
        capture_event = {};
        if (format_log_event(capture_line, &capture_event)) {
            push_log_event(capture_event);
        }
    }
    __atomic_clear(&capture_busy, __ATOMIC_RELEASE);
    return output_length;
}

} // namespace

void install_log_capture(const Config& config) {
    capture_config = config;
    previous_log_vprintf = esp_log_set_vprintf(blackbox_log_vprintf);
}

bool pop_log_event(LogEvent* event) {
    if (event == nullptr) {
        return false;
    }

    portENTER_CRITICAL(&event_ring_lock);
    if (event_ring_used == 0) {
        portEXIT_CRITICAL(&event_ring_lock);
        return false;
    }
    *event = event_ring[event_ring_read];
    event_ring_read = (event_ring_read + 1) % LOG_EVENT_RING_SIZE;
    --event_ring_used;
    portEXIT_CRITICAL(&event_ring_lock);
    return true;
}

void get_capture_statistics(uint32_t* captured,
                            uint32_t* dropped,
                            size_t* pending) {
    portENTER_CRITICAL(&event_ring_lock);
    if (captured != nullptr) {
        *captured = captured_logs;
    }
    if (dropped != nullptr) {
        *dropped = dropped_logs;
    }
    if (pending != nullptr) {
        *pending = event_ring_used;
    }
    portEXIT_CRITICAL(&event_ring_lock);
}

} // namespace BlackboxService::Internal
