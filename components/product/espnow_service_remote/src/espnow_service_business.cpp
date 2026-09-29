/*
 * @Description: ESP-NOW 产品业务消息注册、接收处理与主动请求
 *
 * 本文件直接承接 espnow_link 按 message_id 分发的消息。每个接收回调在一个函数内完成：
 * 传输语义检查 -> payload 长度/字段检查 -> 小端解码 -> 业务处理或应用通知。
 * 回调运行在 espnow_link 任务中，不允许阻塞或执行耗时操作。
 */
#include "espnow_service.h"

#include "esp_random.h"
#include "espnow_codec.h"
#include "espnow_service_internal.h"
#include "freertos/FreeRTOS.h"

namespace EspNowService {
namespace Internal {
namespace {

template<typename Handler>
struct CallbackSlot {
    Handler handler;
    void* context;
};

portMUX_TYPE callback_lock = portMUX_INITIALIZER_UNLOCKED;
CallbackSlot<SwitchResponseHandler> switch_response_slot = {};
CallbackSlot<DataReceivedHandler> data_received_slot = {};
RemoteSwitchStatus remote_switch_status = {};
EspNowLink::MacAddress remote_switch_address = {};

// 固定长度协议。接收时严格匹配，避免接受截断包或未知版本的尾随字段。
constexpr size_t SWITCH_REQUEST_SIZE = 5;
constexpr size_t SWITCH_RESPONSE_SIZE = 7;
constexpr size_t REMOTE_BATTERY_SIZE = 1;
constexpr size_t DATA_REQUEST_SIZE = 4;
constexpr size_t DATA_MESSAGE_SIZE = 40;

uint32_t next_request_id() {
    const uint32_t value = esp_random();
    return value == 0 ? 1 : value;
}

/**
 * @brief 原子读取应用回调及其上下文
 *
 * 只在临界区内复制指针，用户回调在退出临界区后执行。
 */
template<typename Handler>
CallbackSlot<Handler> callback_snapshot(const CallbackSlot<Handler>& slot) {
    portENTER_CRITICAL(&callback_lock);
    const CallbackSlot<Handler> result = slot;
    portEXIT_CRITICAL(&callback_lock);
    return result;
}

/** @brief 原子更新应用回调；handler 为空时同步清除 context。 */
template<typename Handler>
void set_callback(CallbackSlot<Handler>* slot, Handler handler, void* context) {
    portENTER_CRITICAL(&callback_lock);
    slot->handler = handler;
    slot->context = handler == nullptr ? nullptr : context;
    portEXIT_CRITICAL(&callback_lock);
}

/** @brief 生成控制请求、数据请求和对应响应使用的可靠单播选项。 */
EspNowLink::SendOptions reliable_options() {
    EspNowLink::SendOptions options = {};
    options.delivery = EspNowLink::Delivery::RELIABLE;
    return options;
}

/** @brief 请求和响应只接受可靠单播，拒绝广播及尽力传输包。 */
bool is_reliable_unicast(const EspNowLink::Message& message) {
    return message.reliable && !message.destination.is_broadcast();
}

/** @brief 记录本次运行已经收到合法控制包，并锁定对应遥控器地址。 */
void mark_remote_switch_connected(const EspNowLink::MacAddress& source) {
    portENTER_CRITICAL(&callback_lock);
    if (!remote_switch_status.connected || remote_switch_address != source) {
        remote_switch_status = {};
        remote_switch_address = source;
    }
    remote_switch_status.connected = true;
    portEXIT_CRITICAL(&callback_lock);
}

/**
 * @brief 处理开关控制请求
 *
 * button 产品不提供功率输出能力，因此对合法请求返回 NOT_READY。保留处理函数是为了
 * 维持协议对称性，并让对端得到明确响应而不是等待业务超时。
 */
void on_switch_request(const EspNowLink::Message& message, void*) {
    if (!is_reliable_unicast(message) ||
        message.payload_size != SWITCH_REQUEST_SIZE ||
        message.payload[4] > static_cast<uint8_t>(SwitchAction::TOGGLE)) {
        return;
    }
    const uint32_t request_id =
        EspNowLink::Codec::load_le<uint32_t>(message.payload);
    if (request_id == 0) {
        return;
    }
    const SwitchAction action = static_cast<SwitchAction>(message.payload[4]);
    mark_remote_switch_connected(message.source);

    SwitchResponse response = {};
    response.request_id = request_id;
    response.action = action;
    response.result = SwitchResult::NOT_READY;

    uint8_t payload[7] = {};
    const size_t size = encode_switch_response(response, payload, sizeof(payload));
    EspNowLink::send(
        message.source, MSG_SWITCH_RESPONSE, payload, size, reliable_options());
}

/** @brief 接收当前遥控器在控制包之后发送的尽力电量上报。 */
void on_remote_battery(const EspNowLink::Message& message, void*) {
    if (message.reliable ||
        message.destination.is_broadcast() ||
        message.payload_size != REMOTE_BATTERY_SIZE ||
        message.payload[0] > 100) {
        return;
    }

    portENTER_CRITICAL(&callback_lock);
    if (remote_switch_status.connected && remote_switch_address == message.source) {
        remote_switch_status.battery_percent = message.payload[0];
        remote_switch_status.battery_valid = true;
    }
    portEXIT_CRITICAL(&callback_lock);
}

/**
 * @brief 解码开关控制响应并通知 espnow_remote
 *
 * payload:
 * - [0..3] request_id
 * - [4] action
 * - [5] result
 * - [6] output_on
 */
void on_switch_response(const EspNowLink::Message& message, void*) {
    if (!is_reliable_unicast(message) ||
        message.payload_size != SWITCH_RESPONSE_SIZE ||
        message.payload[4] > static_cast<uint8_t>(SwitchAction::TOGGLE) ||
        message.payload[5] > static_cast<uint8_t>(SwitchResult::INTERNAL_ERROR) ||
        message.payload[6] > 1) {
        return;
    }
    const uint32_t request_id =
        EspNowLink::Codec::load_le<uint32_t>(message.payload);
    if (request_id == 0) {
        return;
    }

    const auto slot = callback_snapshot(switch_response_slot);
    if (slot.handler != nullptr) {
        slot.handler(message.source,
                     request_id,
                     static_cast<SwitchAction>(message.payload[4]),
                     static_cast<SwitchResult>(message.payload[5]),
                     message.payload[6] != 0,
                     slot.context);
    }
}

/**
 * @brief 处理实时数据请求
 *
 * button 产品没有本地计量数据，合法请求返回 available=false，避免请求方只能通过超时
 * 判断目标是否支持该业务。
 */
void on_data_request(const EspNowLink::Message& message, void*) {
    if (!is_reliable_unicast(message) ||
        message.payload_size != DATA_REQUEST_SIZE) {
        return;
    }
    const uint32_t request_id =
        EspNowLink::Codec::load_le<uint32_t>(message.payload);
    if (request_id == 0) {
        return;
    }

    DataMessage response = {};
    response.request_id = request_id;
    uint8_t payload[40] = {};
    const size_t size = encode_data_message(response, payload, sizeof(payload));
    EspNowLink::send(
        message.source, MSG_DATA_RESPONSE, payload, size, reliable_options());
}

/**
 * @brief 解码可靠数据响应并通知 espnow_remote
 *
 * request_id 必须非零，用于与当前等待中的读取请求关联。
 */
void on_data_response(const EspNowLink::Message& message, void*) {
    if (!is_reliable_unicast(message) ||
        message.payload_size != DATA_MESSAGE_SIZE ||
        message.payload[4] > 1) {
        return;
    }

    DataMessage data = {};
    data.request_id = EspNowLink::Codec::load_le<uint32_t>(message.payload);
    if (data.request_id == 0) {
        return;
    }
    data.available = message.payload[4] != 0;
    data.data.status_flags = message.payload[5];
    data.data.voltage_mv = EspNowLink::Codec::load_le<uint16_t>(message.payload + 6);
    data.data.current_ua = EspNowLink::Codec::load_le<int32_t>(message.payload + 8);
    data.data.board_temperature_centi_c =
        EspNowLink::Codec::load_le<int16_t>(message.payload + 12);
    data.data.chip_temperature_centi_c =
        EspNowLink::Codec::load_le<int16_t>(message.payload + 14);
    data.data.charge_uah = EspNowLink::Codec::load_le<int64_t>(message.payload + 16);
    data.data.energy_uwh = EspNowLink::Codec::load_le<int64_t>(message.payload + 24);
    data.data.meter_time_ms =
        EspNowLink::Codec::load_le<uint64_t>(message.payload + 32);

    const auto slot = callback_snapshot(data_received_slot);
    if (slot.handler != nullptr) {
        slot.handler(message.source,
                     data.request_id,
                     data.data,
                     data.available,
                     false,
                     slot.context);
    }
}

/**
 * @brief 解码尽力传输的周期数据
 *
 * 周期上报固定要求 request_id=0、available=true、BEST_EFFORT。
 */
void on_periodic_data(const EspNowLink::Message& message, void*) {
    if (message.reliable ||
        message.payload_size != DATA_MESSAGE_SIZE ||
        message.payload[4] != 1 ||
        EspNowLink::Codec::load_le<uint32_t>(message.payload) != 0) {
        return;
    }

    DeviceData data = {};
    data.status_flags = message.payload[5];
    data.voltage_mv = EspNowLink::Codec::load_le<uint16_t>(message.payload + 6);
    data.current_ua = EspNowLink::Codec::load_le<int32_t>(message.payload + 8);
    data.board_temperature_centi_c =
        EspNowLink::Codec::load_le<int16_t>(message.payload + 12);
    data.chip_temperature_centi_c =
        EspNowLink::Codec::load_le<int16_t>(message.payload + 14);
    data.charge_uah = EspNowLink::Codec::load_le<int64_t>(message.payload + 16);
    data.energy_uwh = EspNowLink::Codec::load_le<int64_t>(message.payload + 24);
    data.meter_time_ms = EspNowLink::Codec::load_le<uint64_t>(message.payload + 32);

    const auto slot = callback_snapshot(data_received_slot);
    if (slot.handler != nullptr) {
        slot.handler(message.source, 0, data, true, true, slot.context);
    }
}

} // namespace
} // namespace Internal

esp_err_t init() {
    // service 负责完整启动链路，调用方无需重复初始化 EspNowLink。
    ESP_ERROR_CHECK(EspNowLink::init());

    // 每个业务 ID 直接绑定唯一处理函数，不经过统一入口二次分发。
    esp_err_t ret = EspNowLink::register_handler(
        Internal::MSG_SWITCH_REQUEST, Internal::on_switch_request);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = EspNowLink::register_handler(
        Internal::MSG_SWITCH_RESPONSE, Internal::on_switch_response);
    if (ret != ESP_OK) {
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_REQUEST);
        return ret;
    }
    ret = EspNowLink::register_handler(
        Internal::MSG_REMOTE_BATTERY, Internal::on_remote_battery);
    if (ret != ESP_OK) {
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_RESPONSE);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_REQUEST);
        return ret;
    }
    ret = EspNowLink::register_handler(
        Internal::MSG_DATA_REQUEST, Internal::on_data_request);
    if (ret != ESP_OK) {
        EspNowLink::unregister_handler(Internal::MSG_REMOTE_BATTERY);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_RESPONSE);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_REQUEST);
        return ret;
    }
    ret = EspNowLink::register_handler(
        Internal::MSG_DATA_RESPONSE, Internal::on_data_response);
    if (ret != ESP_OK) {
        EspNowLink::unregister_handler(Internal::MSG_DATA_REQUEST);
        EspNowLink::unregister_handler(Internal::MSG_REMOTE_BATTERY);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_RESPONSE);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_REQUEST);
        return ret;
    }
    ret = EspNowLink::register_handler(
        Internal::MSG_DATA_PERIODIC, Internal::on_periodic_data);
    if (ret != ESP_OK) {
        EspNowLink::unregister_handler(Internal::MSG_DATA_RESPONSE);
        EspNowLink::unregister_handler(Internal::MSG_DATA_REQUEST);
        EspNowLink::unregister_handler(Internal::MSG_REMOTE_BATTERY);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_RESPONSE);
        EspNowLink::unregister_handler(Internal::MSG_SWITCH_REQUEST);
        return ret;
    }

    return ESP_OK;
}

bool get_remote_switch_status(RemoteSwitchStatus& status) {
    portENTER_CRITICAL(&Internal::callback_lock);
    status = Internal::remote_switch_status;
    portEXIT_CRITICAL(&Internal::callback_lock);
    return status.connected;
}

esp_err_t send_remote_battery(const EspNowLink::MacAddress& destination,
                              uint8_t battery_percent,
                              EspNowLink::SendCallback callback,
                              void* context) {
    uint8_t payload[1] = {};
    const size_t size =
        Internal::encode_remote_battery(battery_percent, payload, sizeof(payload));
    if (size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    EspNowLink::SendOptions options = {};
    options.delivery = EspNowLink::Delivery::BEST_EFFORT;
    return EspNowLink::send(destination,
                            Internal::MSG_REMOTE_BATTERY,
                            payload,
                            size,
                            options,
                            callback,
                            context);
}

void set_switch_response_handler(SwitchResponseHandler handler, void* context) {
    Internal::set_callback(&Internal::switch_response_slot, handler, context);
}

/** @brief 注册数据响应/周期上报通知，供 espnow_remote 等请求方消费。 */
void set_data_received_handler(DataReceivedHandler handler, void* context) {
    Internal::set_callback(&Internal::data_received_slot, handler, context);
}

/**
 * @brief 编码并可靠发送开关控制请求
 *
 * 随机非零 request_id 用于关联业务响应；链路 ACK 只表示包已送达，不代表开关动作成功。
 */
esp_err_t send_switch_request(const EspNowLink::MacAddress& destination,
                              SwitchAction action,
                              uint32_t* request_id,
                              EspNowLink::SendCallback callback,
                              void* context) {
    if (action != SwitchAction::OFF &&
        action != SwitchAction::ON &&
        action != SwitchAction::TOGGLE) {
        return ESP_ERR_INVALID_ARG;
    }
    Internal::SwitchRequest request = {};
    request.request_id = Internal::next_request_id();
    request.action = action;
    uint8_t payload[5] = {};
    const size_t size = Internal::encode_switch_request(request, payload, sizeof(payload));
    const esp_err_t ret = EspNowLink::send(destination,
                                          Internal::MSG_SWITCH_REQUEST,
                                          payload,
                                          size,
                                          Internal::reliable_options(),
                                          callback,
                                          context);
    if (ret == ESP_OK && request_id != nullptr) {
        *request_id = request.request_id;
    }
    return ret;
}

/**
 * @brief 编码并可靠发送实时数据读取请求
 *
 * 请求成功入队后通过 request_id 等待 MSG_DATA_RESPONSE。
 */
esp_err_t request_device_data(const EspNowLink::MacAddress& destination,
                              uint32_t* request_id,
                              EspNowLink::SendCallback callback,
                              void* context) {
    const uint32_t id = Internal::next_request_id();
    uint8_t payload[4] = {};
    const size_t size = Internal::encode_data_request(id, payload, sizeof(payload));
    const esp_err_t ret = EspNowLink::send(destination,
                                          Internal::MSG_DATA_REQUEST,
                                          payload,
                                          size,
                                          Internal::reliable_options(),
                                          callback,
                                          context);
    if (ret == ESP_OK && request_id != nullptr) {
        *request_id = id;
    }
    return ret;
}

/** @brief 编码并尽力发送周期数据，不等待链路 ACK 或业务响应。 */
esp_err_t send_periodic_data(const EspNowLink::MacAddress& destination,
                             const DeviceData& data,
                             EspNowLink::SendCallback callback,
                             void* context) {
    Internal::DataMessage message = {};
    message.available = true;
    message.data = data;
    uint8_t payload[40] = {};
    const size_t size = Internal::encode_data_message(message, payload, sizeof(payload));
    EspNowLink::SendOptions options = {};
    options.delivery = EspNowLink::Delivery::BEST_EFFORT;
    return EspNowLink::send(destination,
                            Internal::MSG_DATA_PERIODIC,
                            payload,
                            size,
                            options,
                            callback,
                            context);
}

} // namespace EspNowService
