#ifndef ESPNOW_SERVICE_H
#define ESPNOW_SERVICE_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "espnow_link.h"

namespace EspNowService {

/** 远程输出控制动作。 */
enum class SwitchAction : uint8_t {
    OFF = 0,
    ON = 1,
    TOGGLE = 2,
};

/** 远程输出控制的业务执行结果。 */
enum class SwitchResult : uint8_t {
    OK = 0,
    REJECTED = 1,
    NOT_READY = 2,
    INVALID_ACTION = 3,
    INTERNAL_ERROR = 4,
};

constexpr uint8_t DEVICE_STATUS_OUTPUT_ON = 1U << 0;

/**
 * @brief 产品实时数据快照
 *
 * 字段使用固定宽度整数和明确单位，线上协议由组件逐字段编码，不依赖结构体内存布局。
 * status_flags 的 bit0 表示输出开启，其余位由产品应用自行定义。
 */
struct DeviceData {
    uint16_t voltage_mv = 0;
    int32_t current_ua = 0;
    int16_t board_temperature_centi_c = 0;
    int16_t chip_temperature_centi_c = 0;
    int64_t charge_uah = 0;
    int64_t energy_uwh = 0;
    uint64_t meter_time_ms = 0;
    uint8_t status_flags = 0;
};

/** 本次运行中最近一次控制包对应的远程开关状态。 */
struct RemoteSwitchStatus {
    bool connected = false;
    bool battery_valid = false;
    uint8_t battery_percent = 0;
};

/** 收到控制响应时通知请求方。 */
using SwitchResponseHandler = void (*)(const EspNowLink::MacAddress& source,
                                       uint32_t request_id,
                                       SwitchAction action,
                                       SwitchResult result,
                                       bool output_on,
                                       void* context);

/**
 * @brief 收到数据响应或周期上报时通知应用
 * @param request_id 数据响应对应的请求 ID；周期上报固定为 0
 * @param available false 表示目标当前不能提供请求的数据
 * @param periodic true 表示尽力传输的周期上报，false 表示可靠请求响应
 */
using DataReceivedHandler = void (*)(const EspNowLink::MacAddress& source,
                                     uint32_t request_id,
                                     const DeviceData& data,
                                     bool available,
                                     bool periodic,
                                     void* context);

/** @brief 注册产品协议支持的全部 ESP-NOW 消息回调。 */
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
