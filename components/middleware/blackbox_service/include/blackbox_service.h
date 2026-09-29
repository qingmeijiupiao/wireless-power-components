#ifndef BLACKBOX_SERVICE_H
#define BLACKBOX_SERVICE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace BlackboxService {

struct Statistics {
    uint32_t captured_logs;
    uint32_t dropped_logs;
    uint32_t persist_failures;
    size_t pending_logs;
};

struct Config {
    // INFO 标签由应用选择；数组和字符串必须在整个运行期有效。
    // 默认只捕获 WARN/ERROR，始终排除黑匣子内部标签以避免递归。
    const char* const* info_tags = nullptr;
    size_t info_tag_count = 0;
};

/** @brief 启动阶段单任务调用；重复初始化不会替换正在使用的配置。 */
esp_err_t init(const Config& config = {});

/**
 * @brief Wait until captured logs and queued Flash writes are persisted.
 *
 * Call this before deep sleep and before exporting logs.
 */
esp_err_t sync();

esp_err_t append_event(const char* fmt, ...);
esp_err_t append_text_event(const char* fmt, ...);

/** @brief Return a lock-safe snapshot of the log capture pipeline counters. */
void get_statistics(Statistics* statistics);

} // namespace BlackboxService

#endif
