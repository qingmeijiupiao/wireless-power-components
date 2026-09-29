/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 遥控端 ESP-NOW 应用服务实现，负责请求同步、配对和信道恢复
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-06
 */
#include "espnow_remote.h"

#include <cstdio>

#include "battery_level.h"
#include "blackbox_service.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "wifi_manager.h"

namespace EspNowRemote {
namespace {

constexpr char TAG[] = "EspNowRemote";
constexpr EventBits_t SWITCH_RESPONSE_BIT = BIT0;
constexpr EventBits_t DATA_RESPONSE_BIT = BIT1;
constexpr EventBits_t SWITCH_TRANSPORT_BIT = BIT2;
constexpr EventBits_t DATA_TRANSPORT_BIT = BIT3;

EventGroupHandle_t response_events;

// 控制与数据请求可能并发，而底层信道恢复是单实例流程。这里用互斥锁把
// ensure_peer_channel() 串行化，避免并发提交时底层返回 ESP_ERR_INVALID_STATE
// 直接导致一次操作失败。
SemaphoreHandle_t channel_check_mutex;

// 最近一次业务响应结果，供调用方在等待结束后读取。
EspNowService::SwitchResult* pending_switch_result_out;
bool* pending_switch_output_out;

// 当前产品只允许一个同步开关请求和一个同步数据请求在途。
// 发送完成回调与业务响应回调通过事件位唤醒发起请求的任务。
// 两类事件必须分别等待：链路 ACK 只确认送达，业务响应才确认设备处理结果。
uint32_t pending_switch_id;
uint32_t pending_data_id;
int64_t pending_switch_started_us;
int64_t pending_data_started_us;
EspNowLink::SendResult switch_transport_result = EspNowLink::SendResult::SUBMIT_FAILED;
EspNowLink::SendResult data_transport_result = EspNowLink::SendResult::SUBMIT_FAILED;

const char* action_name(EspNowService::SwitchAction action) {
    switch (action) {
        case EspNowService::SwitchAction::OFF: return "off";
        case EspNowService::SwitchAction::ON: return "on";
        case EspNowService::SwitchAction::TOGGLE: return "toggle";
        default: return "invalid";
    }
}

const char* result_name(EspNowService::SwitchResult result) {
    switch (result) {
        case EspNowService::SwitchResult::OK: return "ok";
        case EspNowService::SwitchResult::REJECTED: return "rejected";
        case EspNowService::SwitchResult::NOT_READY: return "not_ready";
        case EspNowService::SwitchResult::INVALID_ACTION: return "invalid_action";
        default: return "internal_error";
    }
}

void print_mac(const EspNowLink::MacAddress& mac) {
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac.bytes[0], mac.bytes[1], mac.bytes[2],
           mac.bytes[3], mac.bytes[4], mac.bytes[5]);
}

bool controller_peer(EspNowLink::MacAddress* address) {
    EspNowLink::SavedPeer peer = {};
    if (EspNowLink::get_saved_peer(0, &peer) == ESP_OK) {
            if (address != nullptr) {
                *address = peer.address;
            }
            return true;
    }
    return false;
}

/** @brief 在控制事务结束后尽力上报当前显示电量，不影响控制结果。 */
void send_battery_after_control(const EspNowLink::MacAddress& peer) {
    BatteryLevel::Status level = {};
    if (!BatteryLevel::get_status(level)) {
        ESP_LOGW(TAG, "battery status unavailable after control");
        return;
    }

    const esp_err_t ret =
        EspNowService::send_remote_battery(peer, level.displayed_percent);
    ESP_LOGI(TAG, "battery tx percent=%u submit=%s",
             static_cast<unsigned>(level.displayed_percent),
             esp_err_to_name(ret));
}

/**
 * @brief 链路可靠发送完成回调
 * @note 在 espnow_link 任务上下文执行，只记录结果并设置事件位，不执行阻塞操作。
 */
void switch_transport_complete(EspNowLink::SendResult result, uint32_t sequence, void*) {
    switch_transport_result = result;
    printf("[switch-transport] sequence=%lu result=%u elapsed_ms=%lld\n",
           static_cast<unsigned long>(sequence), static_cast<unsigned>(result),
           static_cast<long long>((esp_timer_get_time() - pending_switch_started_us) / 1000));
    xEventGroupSetBits(response_events, SWITCH_TRANSPORT_BIT);
}

/** @brief 数据请求的链路可靠发送完成回调，执行上下文同 switch_transport_complete。 */
void data_transport_complete(EspNowLink::SendResult result, uint32_t sequence, void*) {
    data_transport_result = result;
    printf("[data-transport] sequence=%lu result=%u elapsed_ms=%lld\n",
           static_cast<unsigned long>(sequence), static_cast<unsigned>(result),
           static_cast<long long>((esp_timer_get_time() - pending_data_started_us) / 1000));
    xEventGroupSetBits(response_events, DATA_TRANSPORT_BIT);
}

/**
 * @brief 开关业务响应处理器
 * @note 在 espnow_link 消息分发任务中执行；迟到响应仅打印，不唤醒新的请求。
 */
void switch_response(const EspNowLink::MacAddress& source,
                     uint32_t request_id,
                     EspNowService::SwitchAction action,
                     EspNowService::SwitchResult result,
                     bool output_on,
                     void*) {
    const int64_t elapsed_ms = (esp_timer_get_time() - pending_switch_started_us) / 1000;
    const bool pending = request_id == pending_switch_id;
    printf(pending ? "[switch-rsp] peer=" : "[switch-rsp-late] peer=");
    print_mac(source);
    printf(" request_id=%lu action=%s result=%s output=%u delay_ms=%lld\n",
           static_cast<unsigned long>(request_id), action_name(action), result_name(result),
           output_on ? 1U : 0U, static_cast<long long>(elapsed_ms));
    BlackboxService::append_event("remote: switch id=%lu action=%s result=%s state=%u delay=%lld",
                                  static_cast<unsigned long>(request_id), action_name(action),
                                  result_name(result), output_on ? 1U : 0U,
                                  static_cast<long long>(elapsed_ms));
    if (pending) {
        if (pending_switch_result_out != nullptr) {
            *pending_switch_result_out = result;
        }
        if (pending_switch_output_out != nullptr) {
            *pending_switch_output_out = output_on;
        }
        xEventGroupSetBits(response_events, SWITCH_RESPONSE_BIT);
    }
}

/**
 * @brief 实时数据响应和周期上报处理器
 * @note 周期上报 request_id 固定为 0，仅输出数据，不参与同步请求完成判定。
 */
void data_response(const EspNowLink::MacAddress& source,
                   uint32_t request_id,
                   const EspNowService::DeviceData& data,
                   bool available,
                   bool periodic,
                   void*) {
    const int64_t elapsed_ms = (esp_timer_get_time() - pending_data_started_us) / 1000;
    const bool pending = !periodic && request_id == pending_data_id;
    printf(periodic ? "[data-periodic] peer=" :
           pending ? "[data-rsp] peer=" : "[data-rsp-late] peer=");
    print_mac(source);
    printf(" request_id=%lu available=%u periodic=%u delay_ms=%lld\n",
           static_cast<unsigned long>(request_id), available ? 1U : 0U,
           periodic ? 1U : 0U, static_cast<long long>(elapsed_ms));
    if (available) {
        printf("  voltage=%u mV current=%ld uA board_temp=%.2f C chip_temp=%.2f C\n",
               data.voltage_mv, static_cast<long>(data.current_ua),
               data.board_temperature_centi_c / 100.0,
               data.chip_temperature_centi_c / 100.0);
        printf("  charge=%lld uAh energy=%lld uWh meter_time=%llu ms output=%u flags=0x%02X\n",
               static_cast<long long>(data.charge_uah),
               static_cast<long long>(data.energy_uwh),
               static_cast<unsigned long long>(data.meter_time_ms),
               (data.status_flags & EspNowService::DEVICE_STATUS_OUTPUT_ON) ? 1U : 0U,
               data.status_flags);
    }
    if (pending) {
        xEventGroupSetBits(response_events, DATA_RESPONSE_BIT);
    }
}

/**
 * @brief 使用已保存 peer 的 LMK 探测当前信道，必要时触发加密信道扫描
 * @note 控制与数据请求会并发调用本函数，因此整个流程用 channel_check_mutex 串行化。
 *       如果底层已经在恢复（例如配对任务触发），则等待其结束而不是直接判失败。
 */
esp_err_t ensure_peer_channel(const EspNowLink::MacAddress& peer) {
    if (channel_check_mutex == nullptr ||
        xSemaphoreTake(channel_check_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    const int64_t started_us = esp_timer_get_time();
    const uint16_t timeout_ms = EspNowLink::get_channel_probe_timeout_ms(peer);
    const esp_err_t ret = EspNowLink::recover_peer_channel(peer);
    if (ret == ESP_ERR_INVALID_STATE && EspNowLink::is_recovering_channel()) {
        // 已有恢复在途，等待它结束即可，避免把并发误判为失败。
        printf("[channel-check] join=in_flight\n");
    } else if (ret != ESP_OK) {
        printf("[channel-check] submit=%s\n", esp_err_to_name(ret));
        xSemaphoreGive(channel_check_mutex);
        return ret;
    }

    while (EspNowLink::is_recovering_channel()) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    const esp_err_t result = EspNowLink::get_channel_recovery_result();
    printf("[channel-check] result=%s probe_timeout_ms=%u elapsed_ms=%lld\n",
           esp_err_to_name(result), timeout_ms,
           static_cast<long long>((esp_timer_get_time() - started_us) / 1000));
    xSemaphoreGive(channel_check_mutex);
    return result;
}

} // namespace

esp_err_t init() {
    response_events = xEventGroupCreate();
    if (response_events == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    channel_check_mutex = xSemaphoreCreateMutex();
    if (channel_check_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(WiFiManager::instance().init(), TAG, "WiFi init failed");
    ESP_RETURN_ON_ERROR(EspNowService::init(), TAG, "business service init failed");
    EspNowService::set_switch_response_handler(switch_response);
    EspNowService::set_data_received_handler(data_response);
    return WiFiManager::instance().start_sta_radio(1);
}

esp_err_t send_switch(EspNowService::SwitchAction action,
                      bool wait_response,
                      bool check_channel,
                      EspNowService::SwitchResult* out_result,
                      bool* out_output) {
    EspNowLink::MacAddress peer = {};
    if (!controller_peer(&peer)) {
        printf("[switch] no paired controller; run 'espnow pair'\n");
        return ESP_ERR_NOT_FOUND;
    }
    if (check_channel) {
        ESP_RETURN_ON_ERROR(ensure_peer_channel(peer), TAG, "channel recovery failed");
    }

    pending_switch_result_out = wait_response ? out_result : nullptr;
    pending_switch_output_out = wait_response ? out_output : nullptr;

    xEventGroupClearBits(response_events, SWITCH_RESPONSE_BIT | SWITCH_TRANSPORT_BIT);
    pending_switch_id = 0;
    switch_transport_result = EspNowLink::SendResult::SUBMIT_FAILED;
    pending_switch_started_us = esp_timer_get_time();
    const esp_err_t ret = EspNowService::send_switch_request(
        peer, action, &pending_switch_id, switch_transport_complete, nullptr);
    printf("[switch-tx] peer=");
    print_mac(peer);
    printf(" action=%s request_id=%lu submit=%s\n", action_name(action),
           static_cast<unsigned long>(pending_switch_id), esp_err_to_name(ret));
    if (ret != ESP_OK) {
        pending_switch_result_out = nullptr;
        pending_switch_output_out = nullptr;
        return ret;
    }
    if (!wait_response) {
        pending_switch_result_out = nullptr;
        pending_switch_output_out = nullptr;
        send_battery_after_control(peer);
        return ESP_OK;
    }

    // 先等待链路层 ACK，再等待带相同 request_id 的业务响应。
    EventBits_t bits = xEventGroupWaitBits(
        response_events, SWITCH_TRANSPORT_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(EspNowLink::get_delivery_timeout_ms(peer)));
    if ((bits & SWITCH_TRANSPORT_BIT) == 0 ||
        switch_transport_result != EspNowLink::SendResult::ACKNOWLEDGED) {
        pending_switch_id = 0;
        pending_switch_result_out = nullptr;
        pending_switch_output_out = nullptr;
        send_battery_after_control(peer);
        return ESP_ERR_TIMEOUT;
    }
    bits = xEventGroupGetBits(response_events);
    if ((bits & SWITCH_RESPONSE_BIT) == 0) {
        bits = xEventGroupWaitBits(
            response_events, SWITCH_RESPONSE_BIT, pdTRUE, pdFALSE,
            pdMS_TO_TICKS(EspNowLink::get_response_timeout_ms(peer, 0, 5, 30)));
    } else {
        xEventGroupClearBits(response_events, SWITCH_RESPONSE_BIT);
    }
    pending_switch_id = 0;
    pending_switch_result_out = nullptr;
    pending_switch_output_out = nullptr;
    const esp_err_t result =
        (bits & SWITCH_RESPONSE_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
    send_battery_after_control(peer);
    return result;
}

esp_err_t read_data(bool check_channel) {
    EspNowLink::MacAddress peer = {};
    if (!controller_peer(&peer)) {
        printf("[read] no paired controller\n");
        return ESP_ERR_NOT_FOUND;
    }
    if (check_channel) {
        ESP_RETURN_ON_ERROR(ensure_peer_channel(peer), TAG, "channel recovery failed");
    }

    xEventGroupClearBits(response_events, DATA_RESPONSE_BIT | DATA_TRANSPORT_BIT);
    pending_data_id = 0;
    data_transport_result = EspNowLink::SendResult::SUBMIT_FAILED;
    pending_data_started_us = esp_timer_get_time();
    const esp_err_t ret = EspNowService::request_device_data(
        peer, &pending_data_id, data_transport_complete, nullptr);
    printf("[data-tx] peer=");
    print_mac(peer);
    printf(" request_id=%lu submit=%s\n",
           static_cast<unsigned long>(pending_data_id), esp_err_to_name(ret));
    if (ret != ESP_OK) {
        return ret;
    }

    // 链路 ACK 只表示请求送达；设备数据由后续业务响应返回。
    EventBits_t bits = xEventGroupWaitBits(
        response_events, DATA_TRANSPORT_BIT, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(EspNowLink::get_delivery_timeout_ms(peer)));
    if ((bits & DATA_TRANSPORT_BIT) == 0 ||
        data_transport_result != EspNowLink::SendResult::ACKNOWLEDGED) {
        pending_data_id = 0;
        return ESP_ERR_TIMEOUT;
    }
    bits = xEventGroupGetBits(response_events);
    if ((bits & DATA_RESPONSE_BIT) == 0) {
        bits = xEventGroupWaitBits(
            response_events, DATA_RESPONSE_BIT, pdTRUE, pdFALSE,
            pdMS_TO_TICKS(EspNowLink::get_response_timeout_ms(peer, 0, 5, 30)));
    } else {
        xEventGroupClearBits(response_events, DATA_RESPONSE_BIT);
    }
    pending_data_id = 0;
    return (bits & DATA_RESPONSE_BIT) != 0 ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t start_pairing(bool clear_first) {
    if (clear_first) {
        const esp_err_t clear_ret = EspNowLink::clear_saved_peers();
        printf("[pair] clear peers: %s\n", esp_err_to_name(clear_ret));
        if (clear_ret != ESP_OK) {
            return clear_ret;
        }
    }
    const esp_err_t ret = EspNowLink::start_pairing();
    printf("[pair] scan submitted=%s; saved channel first, then allowed channels\n",
           esp_err_to_name(ret));
    BlackboxService::append_event("remote: pairing start clear=%u result=%s",
                                  clear_first ? 1U : 0U, esp_err_to_name(ret));
    return ret;
}

void stop_pairing() {
    EspNowLink::leave_pairing_mode();
}

esp_err_t clear_peers() {
    EspNowLink::leave_pairing_mode();
    return EspNowLink::clear_saved_peers();
}

esp_err_t set_channel(uint8_t channel) {
    if (channel < 1 || channel > 13) {
        return ESP_ERR_INVALID_ARG;
    }
    return WiFiManager::instance().set_channel(channel);
}

void print_status() {
    uint8_t channel = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    const esp_err_t channel_ret = WiFiManager::instance().get_channel(&channel, &second);
    EspNowLink::LinkStatistics stats = {};
    EspNowLink::get_statistics(&stats);
    printf("active=%u pairing=%u peers=%u channel=%s%u\n",
           EspNowLink::is_active() ? 1U : 0U,
           EspNowLink::is_pairing() ? 1U : 0U,
           static_cast<unsigned>(EspNowLink::get_saved_peer_count()),
           channel_ret == ESP_OK ? "" : "N/A:",
           channel_ret == ESP_OK ? channel : 0);
    printf("tx=%lu retries=%lu ack_timeout=%lu rx=%lu invalid=%lu timing=%lu\n",
           static_cast<unsigned long>(stats.tx_packets),
           static_cast<unsigned long>(stats.tx_retries),
           static_cast<unsigned long>(stats.ack_timeouts),
           static_cast<unsigned long>(stats.rx_packets),
           static_cast<unsigned long>(stats.rx_invalid_packets),
           static_cast<unsigned long>(stats.timing_errors));
    for (size_t i = 0; i < EspNowLink::get_saved_peer_count(); ++i) {
        EspNowLink::SavedPeer peer = {};
        if (EspNowLink::get_saved_peer(i, &peer) == ESP_OK) {
            printf("peer[%u] mac=", static_cast<unsigned>(i));
            print_mac(peer.address);
            printf(" last_channel=%u\n", peer.last_channel);
        }
    }
}

esp_err_t recover_channel() {
    uint8_t current = 1;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    WiFiManager::instance().get_channel(&current, &second);
    uint8_t forced = current == 13 ? 1 : current + 1;
    printf("[recover] force-channel original=%u candidate=%u\n", current, forced);

    // 先主动切换到无法通信的信道，确保后续确实覆盖扫描恢复路径。
    esp_err_t probe = ESP_OK;
    for (uint8_t attempt = 0; attempt < 3 && probe == ESP_OK; ++attempt) {
        ESP_RETURN_ON_ERROR(set_channel(forced), TAG, "force channel failed");
        vTaskDelay(pdMS_TO_TICKS(100));
        probe = read_data(false);
        if (probe == ESP_OK) {
            forced = forced == 13 ? 1 : forced + 1;
        }
    }
    if (probe != ESP_ERR_TIMEOUT) {
        set_channel(current);
        return ESP_FAIL;
    }

    EspNowLink::MacAddress peer = {};
    if (!controller_peer(&peer)) {
        set_channel(current);
        return ESP_ERR_NOT_FOUND;
    }
    // 恢复事件异步投递到配对任务：先等待任务进入恢复态，再等待扫描结束。
    const int64_t started_us = esp_timer_get_time();
    ESP_RETURN_ON_ERROR(EspNowLink::recover_peer_channel(peer), TAG, "recovery submit failed");
    while (!EspNowLink::is_recovering_channel() &&
           esp_timer_get_time() - started_us < 500000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    while (EspNowLink::is_recovering_channel() &&
           esp_timer_get_time() - started_us < 5000000) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return read_data();
}

esp_err_t run_test(int count) {
    if (count < 1 || count > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    int failures = 0;
    const int64_t started_us = esp_timer_get_time();
    for (int i = 0; i < count; ++i) {
        printf("[test] round=%d/%d phase=toggle\n", i + 1, count);
        if (send_switch(EspNowService::SwitchAction::TOGGLE) != ESP_OK) {
            ++failures;
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        printf("[test] round=%d/%d phase=read\n", i + 1, count);
        if (read_data() != ESP_OK) {
            ++failures;
        }
    }
    printf("[test] complete rounds=%d failures=%d elapsed_ms=%lld\n",
           count, failures,
           static_cast<long long>((esp_timer_get_time() - started_us) / 1000));
    return failures == 0 ? ESP_OK : ESP_FAIL;
}

} // namespace EspNowRemote
