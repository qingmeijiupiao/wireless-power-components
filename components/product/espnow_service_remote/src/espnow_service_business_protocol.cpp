/*
 * @Description: ESP-NOW 产品业务 payload 编码
 *
 * 线上协议采用固定偏移、固定宽度和小端格式，不直接发送 C++ 结构体内存，从而避免
 * 对齐填充、编译器 ABI 和 CPU 端序差异。接收解码与业务处理位于 business.cpp。
 */
#include "espnow_service_internal.h"

#include "espnow_codec.h"

namespace EspNowService::Internal {
namespace {

constexpr size_t SWITCH_REQUEST_SIZE = 5;
constexpr size_t SWITCH_RESPONSE_SIZE = 7;
constexpr size_t REMOTE_BATTERY_SIZE = 1;
constexpr size_t DATA_REQUEST_SIZE = 4;
constexpr size_t DATA_MESSAGE_SIZE = 40;

} // namespace

/**
 * @brief 编码开关控制请求
 * @layout [0..3] request_id, [4] action
 * @return 成功时返回 5，参数或容量无效时返回 0
 */
size_t encode_switch_request(const SwitchRequest& request, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < SWITCH_REQUEST_SIZE) {
        return 0;
    }
    EspNowLink::Codec::store_le<uint32_t>(output, request.request_id);
    output[4] = static_cast<uint8_t>(request.action);
    return SWITCH_REQUEST_SIZE;
}

/**
 * @brief 编码开关控制响应
 * @layout [0..3] request_id, [4] action, [5] result, [6] output_on
 * @return 成功时返回 7，参数或容量无效时返回 0
 */
size_t encode_switch_response(const SwitchResponse& response,
                              uint8_t* output,
                              size_t capacity) {
    if (output == nullptr || capacity < SWITCH_RESPONSE_SIZE) {
        return 0;
    }
    EspNowLink::Codec::store_le<uint32_t>(output, response.request_id);
    output[4] = static_cast<uint8_t>(response.action);
    output[5] = static_cast<uint8_t>(response.result);
    output[6] = response.output_on ? 1 : 0;
    return SWITCH_RESPONSE_SIZE;
}

/** @brief 编码远程开关电量，payload 仅包含 0..100 的百分比。 */
size_t encode_remote_battery(uint8_t battery_percent, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < REMOTE_BATTERY_SIZE || battery_percent > 100) {
        return 0;
    }
    output[0] = battery_percent;
    return REMOTE_BATTERY_SIZE;
}

/**
 * @brief 编码实时数据读取请求
 * @layout [0..3] request_id
 * @return 成功时返回 4，参数或容量无效时返回 0
 */
size_t encode_data_request(uint32_t request_id, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < DATA_REQUEST_SIZE) {
        return 0;
    }
    EspNowLink::Codec::store_le<uint32_t>(output, request_id);
    return DATA_REQUEST_SIZE;
}

/**
 * @brief 编码数据响应或周期上报
 *
 * @layout
 * - [0..3]   request_id
 * - [4]      available
 * - [5]      status_flags
 * - [6..39]  电压、电流、温度、电量、能量和计量时间
 *
 * @return 成功时返回 40，参数或容量无效时返回 0
 */
size_t encode_data_message(const DataMessage& message, uint8_t* output, size_t capacity) {
    if (output == nullptr || capacity < DATA_MESSAGE_SIZE) {
        return 0;
    }
    EspNowLink::Codec::store_le<uint32_t>(output, message.request_id);
    output[4] = message.available ? 1 : 0;
    output[5] = message.data.status_flags;
    EspNowLink::Codec::store_le<uint16_t>(output + 6, message.data.voltage_mv);
    EspNowLink::Codec::store_le<int32_t>(output + 8, message.data.current_ua);
    EspNowLink::Codec::store_le<int16_t>(
        output + 12, message.data.board_temperature_centi_c);
    EspNowLink::Codec::store_le<int16_t>(
        output + 14, message.data.chip_temperature_centi_c);
    EspNowLink::Codec::store_le<int64_t>(output + 16, message.data.charge_uah);
    EspNowLink::Codec::store_le<int64_t>(output + 24, message.data.energy_uwh);
    EspNowLink::Codec::store_le<uint64_t>(output + 32, message.data.meter_time_ms);
    return DATA_MESSAGE_SIZE;
}

} // namespace EspNowService::Internal
