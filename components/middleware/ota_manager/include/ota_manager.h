/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: OTA功能以及分区启动相关
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:37:37
 */
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <cstddef>
#include <cstdint>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_partition.h"

namespace OtaManager {

/**
 * @brief 固件长度未知时传给 begin() 的值。
 *
 * 使用该值时，ESP-IDF 会在 OTA 开始时擦除整个目标分区。
 */
constexpr size_t IMAGE_SIZE_UNKNOWN = 0xffffffffU;

enum class State : uint8_t {
    IDLE = 0,        /**< 当前没有 OTA 写入会话。 */
    WRITING,         /**< 正在向目标分区写入固件。 */
    VERIFIED,        /**< 固件已通过校验，等待应用层确认激活。 */
    READY_TO_REBOOT, /**< 固件校验通过，已切换下次启动分区。 */
};

struct Status {
    State                  state            = State::IDLE;
    size_t                 image_size       = 0;       /**< begin() 接收的固件长度。 */
    size_t                 bytes_written    = 0;       /**< 当前会话累计写入字节数。 */
    const esp_partition_t* target_partition = nullptr; /**< 当前 OTA 目标分区。 */
};

/**
 * @brief 开始一次 OTA 固件写入。
 *
 * 自动选择非当前运行分区作为目标分区。中间件仅允许同时存在一个会话。
 *
 * @param image_size 固件总长度；未知时传 IMAGE_SIZE_UNKNOWN。
 * @return ESP_OK 成功，其他值为 ESP-IDF OTA API 返回的错误码。
 */
esp_err_t begin(size_t image_size = IMAGE_SIZE_UNKNOWN);

/**
 * @brief 按顺序写入一块固件数据。
 * @param data 固件数据缓冲区。
 * @param size 数据长度。
 * @return ESP_OK 成功；没有进行中的会话时返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t write(const void* data, size_t size);

/**
 * @brief 结束写入并校验固件。
 *
 * 成功后进入 VERIFIED 状态，不会切换启动分区，也不会主动重启设备。
 *
 * @return ESP_OK 成功；校验失败时返回 ESP-IDF 错误码。
 */
esp_err_t finish();

/**
 * @brief 激活已校验固件，切换下次启动分区。
 *
 * 成功后不会主动重启设备，由应用层决定重启时机。
 *
 * @return ESP_OK 成功；没有已校验固件时返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t activate();

/**
 * @brief 中止写入会话，或放弃尚未激活的已校验固件。
 * @return ESP_OK 成功；当前状态不可放弃时返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t abort();

/** @brief 获取当前 OTA 会话状态快照。 */
Status get_status();

/** @brief 获取当前正在运行的 APP 分区。 */
const esp_partition_t* get_running_partition();

/** @brief 获取已配置为下次启动的 APP 分区。 */
const esp_partition_t* get_boot_partition();

/** @brief 获取当前 OTA 会话选择的目标 APP 分区。 */
const esp_partition_t* get_target_partition();

/** @brief 获取下一次 OTA 写入应使用的 APP 分区。 */
const esp_partition_t* get_next_update_partition();

/** @brief 读取当前正在运行分区的固件描述，包括版本号。 */
esp_err_t get_running_app_description(esp_app_desc_t* app_desc);

/** @brief 读取已配置启动分区的固件描述，包括版本号。 */
esp_err_t get_boot_app_description(esp_app_desc_t* app_desc);

/** @brief 读取当前 OTA 目标分区的固件描述，包括版本号。 */
esp_err_t get_target_app_description(esp_app_desc_t* app_desc);

/**
 * @brief 标记当前运行固件有效，取消 ESP-IDF 自动回滚。
 *
 * 启用 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 后，应在新固件自检通过时调用。
 */
esp_err_t confirm_running_firmware();

/** @brief 查询当前是否存在可用于回滚的固件分区。 */
bool rollback_is_possible();

/**
 * @brief 标记当前固件无效，回滚到上一固件并立即重启。
 *
 * 启用 CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE 后，在新固件自检失败时调用。
 */
esp_err_t rollback_and_reboot();

} // namespace OtaManager

#endif
