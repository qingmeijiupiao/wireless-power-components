#ifndef ESPNOW_SERVICE_INTERNAL_H
#define ESPNOW_SERVICE_INTERNAL_H

#include "espnow_service.h"
namespace EspNowService::Internal {

constexpr uint16_t MSG_SWITCH_REQUEST = 0x0200;
constexpr uint16_t MSG_SWITCH_RESPONSE = 0x0201;
constexpr uint16_t MSG_REMOTE_BATTERY = 0x0202;
constexpr uint16_t MSG_DATA_REQUEST = 0x0210;
constexpr uint16_t MSG_DATA_RESPONSE = 0x0211;
constexpr uint16_t MSG_DATA_PERIODIC = 0x0212;

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

} // namespace EspNowService::Internal

#endif
