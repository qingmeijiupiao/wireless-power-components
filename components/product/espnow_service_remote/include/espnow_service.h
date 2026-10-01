#ifndef ESPNOW_SERVICE_H
#define ESPNOW_SERVICE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "espnow_link.h"
#include "espnow_service_proto.h"

namespace EspNowService {

/** @brief 注册遥控端产品协议支持的全部 ESP-NOW 消息回调。 */
esp_err_t init();

/** @brief 返回本次运行中观察到的远程开关及其最近电量。 */
bool get_remote_switch_status(RemoteSwitchStatus& status);

/**
 * @brief 尽力向已配对设备发送本机电量，不等待链路 ACK
 * @param battery_percent 电量百分比，范围 0..100
 */
esp_err_t send_remote_battery(const EspNowLink::MacAddress& destination,
                              uint8_t battery_percent,
                              EspNowLink::SendCallback callback = nullptr,
                              void* context = nullptr);

/** @brief 注册或清除控制响应通知，handler 为 nullptr 时清除。 */
void set_switch_response_handler(SwitchResponseHandler handler, void* context = nullptr);

/** @brief 注册或清除数据接收通知，handler 为 nullptr 时清除。 */
void set_data_received_handler(DataReceivedHandler handler, void* context = nullptr);

/**
 * @brief 可靠发送开关控制请求
 * @param request_id 返回本次业务请求 ID，可传 nullptr
 * @param callback 链路发送结果回调，可用于诊断 NO_ACK
 */
esp_err_t send_switch_request(const EspNowLink::MacAddress& destination,
                              SwitchAction action,
                              uint32_t* request_id = nullptr,
                              EspNowLink::SendCallback callback = nullptr,
                              void* context = nullptr);

/**
 * @brief 可靠请求目标设备返回实时数据
 * @param request_id 返回本次业务请求 ID，可传 nullptr
 */
esp_err_t request_device_data(const EspNowLink::MacAddress& destination,
                              uint32_t* request_id = nullptr,
                              EspNowLink::SendCallback callback = nullptr,
                              void* context = nullptr);

/**
 * @brief 尽力发送周期数据，不等待业务响应
 *
 * 单播使用已配对 peer 加密，广播由 espnow_link 自动改为明文尽力传输。
 */
esp_err_t send_periodic_data(const EspNowLink::MacAddress& destination,
                             const DeviceData& data,
                             EspNowLink::SendCallback callback = nullptr,
                             void* context = nullptr);

} // namespace EspNowService

#endif
