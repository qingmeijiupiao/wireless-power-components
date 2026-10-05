#ifndef ESPNOW_SERVICE_PROTO_H
#define ESPNOW_SERVICE_PROTO_H

#include <cstddef>
#include <cstdint>

#include "espnow_link.h"

namespace EspNowService {

/** 系列产品角色仅由产品协议解释，通用 Link 不依赖此枚举。 */
enum class PairingRole : uint8_t { UNKNOWN = 0, METER = 1, BUTTON = 2, EMERGENCY_STOP = 3 };

/** @brief 构造产品配对策略：开关单目标替换，功率计允许三只开关。 */
inline EspNowLink::PairingConfig pairing_config(PairingRole role) {
    EspNowLink::PairingConfig config = {};
    config.local_role = static_cast<uint8_t>(role);
    if (role == PairingRole::METER) {
        config.accepted_roles = (1UL << static_cast<uint8_t>(PairingRole::BUTTON)) |
                                (1UL << static_cast<uint8_t>(PairingRole::EMERGENCY_STOP));
    } else {
        config.accepted_roles = 1UL << static_cast<uint8_t>(PairingRole::METER);
        config.peer_limit = 1;
        config.replace_existing = true;
    }
    return config;
}

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
    EspNowLink::MacAddress address = {};
    uint8_t role = 0;
    int64_t last_seen_us = 0;
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

namespace Internal {

// 业务消息 ID。0x0203 为可选扩展：在 0x0201 之外补充拒绝原因和保护掩码，旧端忽略。
constexpr uint16_t MSG_SWITCH_REQUEST = 0x0200;
constexpr uint16_t MSG_SWITCH_RESPONSE = 0x0201;
constexpr uint16_t MSG_REMOTE_BATTERY = 0x0202;
constexpr uint16_t MSG_SWITCH_DETAIL = 0x0203;
constexpr uint16_t MSG_REMOTE_INTERLOCK = 0x0204; /**< 急停可靠单播：1 字节禁止开启标志 0/1。 */
constexpr uint16_t MSG_DATA_REQUEST = 0x0210;
constexpr uint16_t MSG_DATA_RESPONSE = 0x0211;
constexpr uint16_t MSG_DATA_PERIODIC = 0x0212;

// 固定长度协议。接收时严格匹配，避免接受截断包或未知版本的尾随字段。
constexpr size_t SWITCH_REQUEST_SIZE = 5;
constexpr size_t SWITCH_RESPONSE_SIZE = 7;
constexpr size_t REMOTE_BATTERY_SIZE = 1;
constexpr size_t DATA_REQUEST_SIZE = 4;
constexpr size_t DATA_MESSAGE_SIZE = 40;

struct SwitchRequest {
    uint32_t request_id;
    SwitchAction action;
};

struct SwitchResponse {
    uint32_t request_id;
    SwitchAction action;
    SwitchResult result;
    bool output_on;
};

struct DataMessage {
    uint32_t request_id;
    bool available;
    DeviceData data;
};

size_t encode_switch_request(const SwitchRequest& request, uint8_t* output, size_t capacity);
size_t encode_switch_response(const SwitchResponse& response, uint8_t* output, size_t capacity);
size_t encode_remote_battery(uint8_t battery_percent, uint8_t* output, size_t capacity);
size_t encode_data_request(uint32_t request_id, uint8_t* output, size_t capacity);
size_t encode_data_message(const DataMessage& data, uint8_t* output, size_t capacity);

/** @brief 构造业务请求/响应统一使用的可靠单播选项。 */
EspNowLink::SendOptions reliable_options();

/** @brief 请求和响应只接受可靠单播，拒绝广播及尽力传输包。 */
bool is_reliable_unicast(const EspNowLink::Message& message);

} // namespace Internal

} // namespace EspNowService

#endif
