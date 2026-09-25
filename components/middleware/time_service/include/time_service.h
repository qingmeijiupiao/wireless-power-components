/*
 * @Description: 基于 esp_netif_sntp 的中间件时间同步服务公开接口
 */
#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H

#include <ctime>

#include "esp_err.h"

namespace TimeService {

/**
 * @brief 初始化时区、SNTP 和异步事件后台任务
 *
 * 在首次 STA 连接前调用一次。系统时间保持 UTC 语义，本地时间通过进程时区
 * 转换为 UTC+8。
 */
esp_err_t init();

/** @brief 查询是否至少成功完成过一次 SNTP 同步 */
bool is_synchronized();

/** @brief 获取当前 UTC Unix 时间戳，单位为秒 */
time_t now_utc();

/**
 * @brief 获取当前 UTC+8 本地时间
 *
 * @param out 输出缓冲区
 * @return 首次同步完成后返回 true；未同步或参数无效时返回 false
 */
bool get_local_time(struct tm* out);

/** @brief 获取最近一次成功同步的 UTC Unix 时间戳 */
time_t last_sync_utc();

} // namespace TimeService

#endif
