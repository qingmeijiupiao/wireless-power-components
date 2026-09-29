/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 遥控端 ESP-NOW 应用服务，封装配对、可靠请求、响应等待和信道恢复
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-06
 */
#ifndef ESPNOW_REMOTE_H
#define ESPNOW_REMOTE_H

#include "esp_err.h"
#include "espnow_link.h"
#include "espnow_service.h"

namespace EspNowRemote {

/**
 * @brief 初始化遥控端 ESP-NOW 应用服务
 *
 * 依次初始化 WiFiManager、EspNowLink 和 EspNowService，注册开关响应与
 * 数据响应处理器，最后在信道 1 启动 STA 射频。
 *
 * @note 调用前必须完成 NVS 初始化；该函数只能在应用启动阶段调用一次。
 * @return ESP_OK 初始化成功，ESP_ERR_NO_MEM 事件组创建失败，其他错误来自底层组件
 */
esp_err_t init();

/**
 * @brief 向已配对控制器发送开关请求
 *
 * @param action OFF、ON 或 TOGGLE
 * @param wait_response true 等待链路 ACK 和业务响应，false 仅提交发送
 * @param check_channel true 发送前执行加密信道探测与恢复
 * @param out_result 可选，返回对端业务层执行结果（仅 wait_response=true 时有效）
 * @param out_output 可选，返回对端执行后的实际输出状态（仅 wait_response=true 时有效）
 * @return ESP_OK 收到链路 ACK 且收到业务响应（业务结果可能为 REJECTED），
 *         ESP_ERR_NOT_FOUND 未找到控制器，ESP_ERR_TIMEOUT 未收到 ACK 或业务响应，
 *         其他错误来自底层链路
 * @note 等待模式会阻塞当前调用任务，不应从 WiFi 或 ESP-NOW 回调中调用。
 * @note 收到业务响应即返回 ESP_OK，即使对端拒绝执行；调用方需通过 out_result 判断。
 */
esp_err_t send_switch(EspNowService::SwitchAction action,
                      bool wait_response = true,
                      bool check_channel = true,
                      EspNowService::SwitchResult* out_result = nullptr,
                      bool* out_output = nullptr);

/**
 * @brief 请求已配对控制器返回实时测量数据
 * @param check_channel true 请求前执行加密信道探测与恢复
 * @return ESP_OK 收到有效业务响应，ESP_ERR_NOT_FOUND 未找到控制器，
 *         ESP_ERR_TIMEOUT 未收到 ACK 或业务响应，其他错误来自底层链路
 * @note 该函数会阻塞当前调用任务直到请求完成或超时。
 */
esp_err_t read_data(bool check_channel = true);

/**
 * @brief 启动遥控端配对扫描
 * @param clear_first true 先清除运行期 peer 和 NVS 配对记录
 * @return ESP_OK 已提交配对事件，其他值表示清除或配对启动失败
 */
esp_err_t start_pairing(bool clear_first);

/**
 * @brief 设置当前 WiFi/ESP-NOW 主信道
 * @param channel 信道号，范围 1-13
 * @return ESP_OK 设置成功，ESP_ERR_INVALID_ARG 信道越界，其他错误来自 WiFiManager
 */
esp_err_t set_channel(uint8_t channel);

/**
 * @brief 执行离线信道构造、加密扫描恢复和数据验证流程
 * @return ESP_OK 恢复后读取数据成功，其他值表示构造离线信道或恢复失败
 * @note 该接口主要用于 Shell 诊断，会阻塞当前调用任务。
 */
esp_err_t recover_channel();

/**
 * @brief 重复执行远程切换和数据读取测试
 * @param count 测试轮数，范围 1-100
 * @return ESP_OK 全部操作成功，ESP_ERR_INVALID_ARG 参数越界，ESP_FAIL 存在失败
 */
esp_err_t run_test(int count);

/** @brief 停止当前配对扫描；未处于配对状态时调用无副作用。 */
void stop_pairing();

/**
 * @brief 停止配对并清除全部运行期和持久化 peer
 * @return ESP_OK 清除成功，其他值来自 EspNowLink
 */
esp_err_t clear_peers();

/** @brief 向标准输出打印链路状态、统计计数和已保存 peer。 */
void print_status();

} // namespace EspNowRemote

#endif
