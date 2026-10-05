#include "espnow_link.h"

#include <atomic>
#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "espnow_codec.h"
#include "espnow_link_internal.h"
#include "espnow_pairing_internal.h"
#include "espnow_protocol.h"
#include "wifi_manager.h"

namespace EspNowLink {
namespace Internal {
namespace {

constexpr char TAG[] = "EspNowPairing";
constexpr uint32_t DISCOVERY_WAIT_MS = 150;
constexpr uint32_t RESPONSE_WAIT_MS = 600;
constexpr uint32_t TRANSACTION_WAIT_MS = 2000;
constexpr size_t MAX_CANDIDATES = MAX_SAVED_PEERS + 1;
constexpr uint16_t HANDLER_IDS[] = {
    MSG_CHANNEL_PROBE,    MSG_CHANNEL_PROBE_RESPONSE, MSG_DISCOVERY_PING,  MSG_DISCOVERY_RESPONSE,
    MSG_DISCOVERY_INFO,   MSG_PAIR_REQUEST,           MSG_PAIR_REQUEST_V2, MSG_PAIR_RESPONSE,
    MSG_PAIR_RESPONSE_V2, MSG_PAIR_CONFIRM,           MSG_PAIR_RESULT,     MSG_PAIR_RESULT_QUERY,
};

enum class EventType : uint8_t { LISTEN, START, RECOVER, MESSAGE, RESPONSE_SENT, CONFIRM_SENT, RESULT_SENT };
enum class Phase : uint8_t {
    IDLE,
    LISTENING,
    DISCOVERY,
    RESPONSE,
    CONFIRM,
    RESULT,
    RESPONDING,
    WAIT_CONFIRM,
    SEND_RESULT,
    RECOVERY,
};
struct Event {
    EventType type = EventType::MESSAGE;
    MacAddress source = {};
    uint16_t message_id = 0;
    uint32_t nonce = 0;
    uint32_t token = 0;
    uint32_t timeout_ms = 0;
    uint8_t channel = 0;
    uint8_t role = 0;
    uint8_t lmk[KEY_SIZE] = {};
    esp_err_t error = ESP_OK;
    SendResult sent = SendResult::SUBMIT_FAILED;
};
struct Candidate {
    MacAddress address = {};
    uint8_t role = 0;
    bool extended = false;
};
struct Transaction {
    MacAddress peer = {};
    uint32_t nonce = 0;
    uint32_t token = 0;
    uint8_t channel = 0;
    uint8_t role = 0;
    uint8_t lmk[KEY_SIZE] = {};
    bool extended = false;
    bool committed = false;
};

QueueHandle_t events = nullptr;
TaskHandle_t worker = nullptr;
std::atomic_bool initialized_pairing{false};
std::atomic_bool session_active{false};
std::atomic_bool recovery_active{false};
std::atomic_bool cancel_requested{false};
std::atomic<esp_err_t> recovery_result{ESP_ERR_INVALID_STATE};
portMUX_TYPE result_lock = portMUX_INITIALIZER_UNLOCKED;
PairingConfig config = {};
PairingResult last_result = {};
Phase phase = Phase::IDLE;
Transaction transaction = {};
Transaction completed = {};
Candidate candidates[MAX_CANDIDATES] = {};
size_t candidate_count = 0;
bool candidate_overflow = false;
bool confirm_pending = false;
bool peers_suspended = false;
bool initiator = false;
TickType_t window_deadline = 0;
TickType_t phase_deadline = 0;
bool unlimited_window = false;
uint8_t restore_channel = 1;
uint8_t channels[14] = {};
uint8_t channel_count = 0;
uint8_t channel_index = 0;
uint8_t channel_attempt = 0;
uint8_t request_attempt = 0;
uint32_t next_token = 1;
esp_err_t scan_error = ESP_ERR_NOT_FOUND;

bool elapsed(TickType_t deadline) { return static_cast<int32_t>(xTaskGetTickCount() - deadline) >= 0; }
TickType_t after(uint32_t ms) { return xTaskGetTickCount() + pdMS_TO_TICKS(ms); }
uint32_t random_nonce() {
    const uint32_t value = esp_random();
    return value == 0 ? 1 : value;
}
MacAddress local_mac() {
    MacAddress address = {};
    const MAC_t mac = WiFiManager::instance().get_mac(WIFI_IF_STA);
    memcpy(address.bytes, mac.bytes, MAC_ADDRESS_SIZE);
    return address;
}
void enqueue(const Event &event) {
    if (events != nullptr && xQueueSend(events, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full");
    }
}
bool role_allowed(uint8_t role) {
    return role < 32 && (config.accepted_roles == 0 || (config.accepted_roles & (1UL << role)) != 0);
}
PeerConfig peer_config(const Transaction &tx) {
    PeerConfig peer = {};
    peer.address = tx.peer;
    peer.channel = tx.channel;
    peer.encrypted = true;
    memcpy(peer.lmk, tx.lmk, KEY_SIZE);
    return peer;
}
void rollback_transaction() {
    const MacAddress empty = {};
    if (transaction.nonce != 0 && transaction.peer != empty && !transaction.committed) {
        remove_peer(transaction.peer);
        restore_peers();
        peers_suspended = false; // 已在此恢复，避免 finish() 重复恢复。
    }
    transaction = {};
    confirm_pending = false;
}
// 响应端若仍持有该设备的加密运行期 peer，驱动会丢弃其明文配对请求；
// 收到发现帧时临时移除该运行期 peer（NVS 不变），配对结束再恢复。
void suspend_peer(const MacAddress &address) {
    if (has_peer(address)) {
        remove_peer(address);
        peers_suspended = true;
        ESP_LOGI(TAG, "pairing: existing peer suspended for plaintext handshake");
    }
}
void report(esp_err_t error) {
    portENTER_CRITICAL(&result_lock);
    ++last_result.serial;
    last_result.peer = transaction.peer;
    last_result.channel = transaction.channel;
    last_result.error = error;
    last_result.initiator = initiator;
    last_result.legacy = !transaction.extended;
    portEXIT_CRITICAL(&result_lock);
    ESP_LOGI(TAG, "pairing complete initiator=%u peer=%02x:%02x:%02x:%02x:%02x:%02x channel=%u legacy=%u result=%s",
             initiator, transaction.peer.bytes[0], transaction.peer.bytes[1], transaction.peer.bytes[2],
             transaction.peer.bytes[3], transaction.peer.bytes[4], transaction.peer.bytes[5], transaction.channel,
             !transaction.extended, esp_err_to_name(error));
}
void finish(esp_err_t error) {
    report(error);
    rollback_transaction();
    if (peers_suspended) {
        restore_peers();
        peers_suspended = false;
    }
    if (initiator && error != ESP_OK && is_active()) {
        WiFiManager::instance().set_channel(restore_channel);
    }
    phase = Phase::IDLE;
    session_active.store(false);
}
void send_callback_event(EventType type, SendResult result, void *context) {
    Event event = {};
    event.type = type;
    event.token = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(context));
    event.sent = result;
    enqueue(event);
}
void response_callback(SendResult result, uint32_t, void *context) {
    send_callback_event(EventType::RESPONSE_SENT, result, context);
}
void confirm_callback(SendResult result, uint32_t, void *context) {
    send_callback_event(EventType::CONFIRM_SENT, result, context);
}
void result_callback(SendResult result, uint32_t, void *context) {
    send_callback_event(EventType::RESULT_SENT, result, context);
}
SendOptions plain_options() {
    SendOptions options = {};
    options.delivery = Delivery::BEST_EFFORT;
    options.allow_plaintext = true;
    return options;
}
SendOptions reliable_options() {
    SendOptions options = {};
    options.timeout_mode = TimeoutMode::FIXED;
    options.ack_timeout_ms = 60;
    return options;
}
esp_err_t send_nonce(const MacAddress &peer, uint16_t id, uint32_t nonce, const SendOptions &options,
                     SendCallback callback = nullptr, uint32_t token = 0) {
    uint8_t payload[4] = {};
    encode_nonce(nonce, payload, sizeof(payload));
    return send(peer, id, payload, sizeof(payload), options, callback,
                reinterpret_cast<void *>(static_cast<uintptr_t>(token)));
}
void on_message(const Message &message, void *) {
    Event event = {};
    event.source = message.source;
    event.channel = message.channel;
    event.message_id = message.message_id;
    switch (message.message_id) {
    case MSG_DISCOVERY_RESPONSE:
        if (!decode_discovery_response(message, local_mac(), &event.nonce)) {
            return;
        }
        break;
    case MSG_DISCOVERY_INFO:
        // [target MAC, nonce LE32, protocol=2, role].
        if (message.payload_size != 12 || message.payload[10] != 2 || message.payload[11] >= 32 ||
            memcmp(message.payload, local_mac().bytes, MAC_ADDRESS_SIZE) != 0) {
            return;
        }
        event.nonce = Codec::load_le<uint32_t>(message.payload + 6);
        event.role = message.payload[11];
        break;
    case MSG_PAIR_REQUEST_V2:
        if (message.payload_size != 5 || message.payload[4] >= 32) {
            return;
        }
        event.nonce = Codec::load_le<uint32_t>(message.payload);
        event.role = message.payload[4];
        break;
    case MSG_PAIR_RESPONSE:
    case MSG_PAIR_RESPONSE_V2:
        if (!decode_pair_response(message, &event.nonce, event.lmk)) {
            return;
        }
        break;
    case MSG_PAIR_RESULT:
        if (message.destination.is_broadcast()) {
            if (message.payload_size != 14 || memcmp(message.payload, local_mac().bytes, MAC_ADDRESS_SIZE) != 0) {
                return;
            }
            event.nonce = Codec::load_le<uint32_t>(message.payload + 6);
            event.error = static_cast<esp_err_t>(Codec::load_le<uint32_t>(message.payload + 10));
        } else {
            if (message.payload_size != 8) {
                return;
            }
            event.nonce = Codec::load_le<uint32_t>(message.payload);
            event.error = static_cast<esp_err_t>(Codec::load_le<uint32_t>(message.payload + 4));
        }
        // 成功结果必须走可靠单播；明文尽力结果仅允许容量拒绝。
        if (!message.reliable && event.error != ESP_ERR_NO_MEM) {
            return;
        }
        break;
    default:
        if ((message.message_id == MSG_PAIR_CONFIRM || message.message_id == MSG_PAIR_RESULT_QUERY) &&
            (!message.reliable || message.destination.is_broadcast())) {
            return;
        }
        if (!decode_nonce(message, &event.nonce)) {
            return;
        }
        break;
    }
    if (event.nonce != 0) {
        enqueue(event);
    }
}
void discovery_response(const Event &event) {
    uint8_t payload[12] = {};
    encode_discovery_response(event.source, event.nonce, payload, sizeof(payload));
    // V2 能力先发送，再发送原长度 V1 响应，旧端无需改变解析逻辑。
    payload[10] = 2;
    payload[11] = config.local_role;
    send(BROADCAST_ADDRESS, MSG_DISCOVERY_INFO, payload, sizeof(payload), plain_options());
    send(BROADCAST_ADDRESS, MSG_DISCOVERY_RESPONSE, payload, 10, plain_options());
}
void collect_candidate(const Event &event) {
    if (event.nonce != transaction.nonce || event.channel != channels[channel_index]) {
        return;
    }
    size_t index = 0;
    while (index < candidate_count && candidates[index].address != event.source) {
        ++index;
    }
    if (index == candidate_count) {
        if (candidate_count == MAX_CANDIDATES) {
            candidate_overflow = true;
            return;
        }
        candidates[candidate_count++].address = event.source;
    }
    if (event.message_id == MSG_DISCOVERY_INFO) {
        candidates[index].extended = true;
        candidates[index].role = event.role;
    }
}
void scan_channel() {
    rollback_transaction();
    candidate_count = 0;
    candidate_overflow = false;
    for (auto &candidate : candidates) {
        candidate = {};
    }
    transaction.nonce = random_nonce();
    transaction.channel = channels[channel_index];
    transaction.token = next_token++;
    phase = Phase::DISCOVERY;
    phase_deadline = after(DISCOVERY_WAIT_MS);
    const esp_err_t error = WiFiManager::instance().set_channel(transaction.channel);
    if (error != ESP_OK) {
        scan_error = error;
        return;
    }
    const esp_err_t sent = send_nonce(BROADCAST_ADDRESS, MSG_DISCOVERY_PING, transaction.nonce, plain_options());
    if (sent != ESP_OK) {
        scan_error = sent;
    }
}
void next_channel() {
    if (++channel_attempt >= 2) {
        channel_attempt = 0;
        channel_index = static_cast<uint8_t>((channel_index + 1) % channel_count);
    }
    scan_channel();
}
esp_err_t request_pair() {
    if (transaction.extended) {
        uint8_t payload[5] = {};
        encode_nonce(transaction.nonce, payload, sizeof(payload));
        payload[4] = config.local_role;
        return send(transaction.peer, MSG_PAIR_REQUEST_V2, payload, sizeof(payload), plain_options());
    }
    return send_nonce(transaction.peer, MSG_PAIR_REQUEST, transaction.nonce, plain_options());
}
void choose_candidate() {
    Candidate chosen = {};
    size_t accepted = 0;
    for (size_t i = 0; i < candidate_count; ++i) {
        const Candidate &candidate = candidates[i];
        if (candidate.extended ? role_allowed(candidate.role) : config.allow_legacy) {
            chosen = candidate;
            ++accepted;
        }
    }
    if (candidate_overflow || accepted > 1) {
        finish(ESP_ERR_INVALID_RESPONSE); // 多候选不按响应先后误绑定。
        return;
    }
    if (accepted == 0) {
        next_channel();
        return;
    }
    transaction.peer = chosen.address;
    transaction.extended = chosen.extended;
    transaction.role = chosen.role;
    PeerConfig temporary = {};
    temporary.address = chosen.address;
    temporary.channel = transaction.channel;
    const esp_err_t error = add_peer(temporary);
    if (error != ESP_OK) {
        scan_error = error;
        next_channel();
        return;
    }
    phase = Phase::RESPONSE;
    request_attempt = 1;
    phase_deadline = after(RESPONSE_WAIT_MS);
    scan_error = request_pair();
}
void prepare_channels() {
    wifi_country_t country = {};
    WiFiManager::instance().get_country(&country);
    const uint8_t first = country.schan == 0 ? 1 : country.schan;
    const uint8_t count = country.nchan == 0 ? 13 : country.nchan;
    uint8_t current = 1;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    WiFiManager::instance().get_channel(&current, &second);
    restore_channel = current;
    SavedPeer saved = {};
    const uint8_t preferred = read_saved_peer(0, &saved) == ESP_OK ? saved.last_channel : current;
    channel_count = 0;
    if (preferred >= first && preferred < first + count) {
        channels[channel_count++] = preferred;
    }
    for (uint8_t channel = first; channel < first + count && channel <= 14; ++channel) {
        if (channel != preferred) {
            channels[channel_count++] = channel;
        }
    }
    if (channel_count == 0) {
        channels[channel_count++] = 1;
    }
    channel_index = 0;
    channel_attempt = 0;
}
bool same_transaction(const Event &event) {
    return transaction.nonce != 0 && event.source == transaction.peer && event.nonce == transaction.nonce;
}
bool capacity_available(const MacAddress &address) {
    const PeerStore store = load_store();
    for (const auto &peer : store.peers) {
        if (peer.used && memcmp(peer.mac, address.bytes, MAC_ADDRESS_SIZE) == 0) {
            return true;
        }
    }
    return store.count < config.peer_limit || config.replace_existing;
}
void send_result(const Transaction &tx, esp_err_t error, bool track) {
    uint8_t payload[8] = {};
    encode_nonce(tx.nonce, payload, sizeof(payload));
    Codec::store_le<uint32_t>(payload + 4, static_cast<uint32_t>(error));
    send(tx.peer, MSG_PAIR_RESULT, payload, sizeof(payload), reliable_options(), track ? result_callback : nullptr,
         reinterpret_cast<void *>(static_cast<uintptr_t>(tx.token)));
}
void respond_pair(const Event &event) {
    const bool extended = event.message_id == MSG_PAIR_REQUEST_V2;
    if (phase != Phase::LISTENING) {
        // 重复请求复用密钥；发送结果回调前不接受第二个请求。
        if (phase != Phase::WAIT_CONFIRM || !same_transaction(event)) {
            return;
        }
        PeerConfig temporary = {};
        temporary.address = transaction.peer;
        temporary.channel = transaction.channel;
        if (add_peer(temporary) != ESP_OK) {
            return;
        }
    } else {
        if ((extended && !role_allowed(event.role)) || (!extended && !config.allow_legacy)) {
            return;
        }
        if (!capacity_available(event.source)) {
            ESP_LOGW(TAG, "pairing rejected: saved peer capacity exhausted");
            if (extended) {
                // 广播拒绝包含目标地址，无需占用运行期 peer 槽位。
                uint8_t payload[14] = {};
                encode_discovery_response(event.source, event.nonce, payload, sizeof(payload));
                Codec::store_le<uint32_t>(payload + 10, ESP_ERR_NO_MEM);
                send(BROADCAST_ADDRESS, MSG_PAIR_RESULT, payload, sizeof(payload), plain_options());
            }
            return;
        }
        transaction = {};
        transaction.peer = event.source;
        transaction.nonce = event.nonce;
        transaction.channel = event.channel;
        transaction.role = event.role;
        transaction.extended = extended;
        transaction.token = next_token++;
        // 同 MAC 修复优先复用保存的 LMK，失败尝试不会永久破坏旧密钥。
        const PeerStore store = load_store();
        bool reused = false;
        for (const auto &peer : store.peers) {
            if (peer.used && memcmp(peer.mac, event.source.bytes, MAC_ADDRESS_SIZE) == 0) {
                memcpy(transaction.lmk, peer.lmk, KEY_SIZE);
                if (!extended) {
                    transaction.role = peer.reserved_role;
                }
                reused = true;
                break;
            }
        }
        if (!reused) {
            esp_fill_random(transaction.lmk, KEY_SIZE);
        }
        PeerConfig temporary = {};
        temporary.address = event.source;
        temporary.channel = event.channel;
        const esp_err_t error = add_peer(temporary);
        if (error != ESP_OK) {
            report(error);
            rollback_transaction();
            return;
        }
    }
    uint8_t payload[4 + KEY_SIZE] = {};
    encode_pair_response(transaction.nonce, transaction.lmk, payload, sizeof(payload));
    phase = Phase::RESPONDING;
    phase_deadline = after(TRANSACTION_WAIT_MS);
    const esp_err_t error =
        send(transaction.peer, extended ? MSG_PAIR_RESPONSE_V2 : MSG_PAIR_RESPONSE, payload, sizeof(payload),
             plain_options(), response_callback, reinterpret_cast<void *>(static_cast<uintptr_t>(transaction.token)));
    if (error != ESP_OK) {
        report(error);
        rollback_transaction();
        phase = Phase::LISTENING;
    }
}
void accept_response(const Event &event) {
    if (!same_transaction(event) || transaction.extended != (event.message_id == MSG_PAIR_RESPONSE_V2)) {
        return;
    }
    memcpy(transaction.lmk, event.lmk, KEY_SIZE);
    const esp_err_t error = add_peer(peer_config(transaction));
    if (error != ESP_OK) {
        scan_error = error;
        next_channel();
        return;
    }
    phase = Phase::CONFIRM;
    phase_deadline = after(TRANSACTION_WAIT_MS);
    scan_error = send_nonce(transaction.peer, MSG_PAIR_CONFIRM, transaction.nonce, reliable_options(), confirm_callback,
                            transaction.token);
    if (scan_error != ESP_OK) {
        next_channel();
    }
}
void accept_confirm(const Event &event) {
    if (phase != Phase::WAIT_CONFIRM || !same_transaction(event)) {
        return;
    }
    const esp_err_t error =
        save_peer(peer_config(transaction), transaction.channel, transaction.role, config.replace_existing);
    if (error == ESP_OK) {
        transaction.committed = true;
        completed = transaction;
    }
    if (!transaction.extended) {
        finish(error);
        return;
    }
    phase = Phase::SEND_RESULT;
    phase_deadline = after(TRANSACTION_WAIT_MS);
    scan_error = error;
    send_result(transaction, error, true);
}
void commit_initiator() {
    const esp_err_t error =
        save_peer(peer_config(transaction), transaction.channel, transaction.role, config.replace_existing);
    transaction.committed = error == ESP_OK;
    finish(error);
}

// 信道恢复使用保存的 LMK，不触发发现或替换绑定。
bool probe_channel(const MacAddress &peer, uint8_t channel, uint16_t timeout_ms) {
    const uint32_t nonce = random_nonce();
    uint8_t payload[4] = {};
    encode_nonce(nonce, payload, sizeof(payload));
    uint8_t frame[FRAME_HEADER_SIZE + sizeof(payload)] = {};
    size_t size = 0;
    if (encode_frame(0, MSG_CHANNEL_PROBE, random_nonce(), random_nonce(), payload, sizeof(payload), frame,
                     sizeof(frame), &size) != ESP_OK) {
        return false;
    }
    alignas(esp_now_switch_channel_t) uint8_t buffer[sizeof(esp_now_switch_channel_t) + sizeof(frame)] = {};
    auto *request = reinterpret_cast<esp_now_switch_channel_t *>(buffer);
    request->type = WIFI_OFFCHAN_TX_REQ;
    request->channel = channel;
    request->sec_channel = WIFI_SECOND_CHAN_NONE;
    request->wait_time_ms = timeout_ms;
    request->op_id = static_cast<uint8_t>(nonce);
    memcpy(request->dest_mac, peer.bytes, MAC_ADDRESS_SIZE);
    request->data_len = static_cast<uint16_t>(size);
    memcpy(request->data, frame, size);
    {
        LifecycleGuard guard;
        if (!is_active() || esp_now_switch_channel_tx(request) != ESP_OK) {
            return false;
        }
    }
    const TickType_t deadline = after(timeout_ms);
    Event event = {};
    while (!elapsed(deadline) && is_active() && !cancel_requested.load()) {
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(5)) != pdTRUE) {
            continue;
        }
        if (event.type != EventType::MESSAGE) {
            continue;
        }
        if (event.message_id == MSG_CHANNEL_PROBE_RESPONSE && event.source == peer && event.nonce == nonce) {
            return true;
        }
        if (event.message_id == MSG_CHANNEL_PROBE && has_peer(event.source)) {
            send_nonce(event.source, MSG_CHANNEL_PROBE_RESPONSE, event.nonce, plain_options());
        }
    }
    return false;
}
void recover_channel(const MacAddress &peer) {
    prepare_channels();
    SavedPeer saved = {};
    for (size_t i = 0; i < saved_peer_count(); ++i) {
        if (read_saved_peer(i, &saved) == ESP_OK && saved.address == peer) {
            for (uint8_t n = 0; n < channel_count; ++n) {
                if (channels[n] == saved.last_channel) {
                    const uint8_t previous = channels[0];
                    channels[0] = channels[n];
                    channels[n] = previous;
                    break;
                }
            }
            break;
        }
    }
    const uint16_t timeout_ms = get_channel_probe_timeout_ms(peer);
    esp_err_t error = ESP_ERR_TIMEOUT;
    for (uint8_t i = 0; i < channel_count && is_active() && !cancel_requested.load(); ++i) {
        const uint8_t attempts = i == 0 ? 3 : 2;
        for (uint8_t n = 0; n < attempts; ++n) {
            if (probe_channel(peer, channels[i], timeout_ms)) {
                error = WiFiManager::instance().set_channel(channels[i]);
                if (error == ESP_OK) {
                    error = update_peer_channel(peer, channels[i]);
                }
                recovery_result.store(error);
                recovery_active.store(false);
                phase = Phase::IDLE;
                return;
            }
        }
    }
    if (is_active()) {
        WiFiManager::instance().set_channel(restore_channel);
    }
    recovery_result.store(error);
    recovery_active.store(false);
    phase = Phase::IDLE;
    ESP_LOGI(TAG, "peer channel recovery result=%s", esp_err_to_name(error));
}
void process_event(const Event &event) {
    if (event.type == EventType::START || event.type == EventType::LISTEN) {
        if (phase != Phase::IDLE) {
            return;
        }
        initiator = event.type == EventType::START;
        unlimited_window = !initiator && event.timeout_ms == 0;
        window_deadline = after(initiator ? config.scan_timeout_ms : event.timeout_ms);
        scan_error = ESP_ERR_NOT_FOUND;
        if (initiator) {
            prepare_channels();
            scan_channel();
        } else {
            phase = Phase::LISTENING;
        }
        return;
    }
    if (event.type == EventType::RECOVER) {
        phase = Phase::RECOVERY;
        recover_channel(event.source);
        return;
    }
    if (event.type != EventType::MESSAGE) {
        if (event.token != transaction.token || transaction.nonce == 0) {
            return;
        }
        if (event.type == EventType::RESPONSE_SENT && phase == Phase::RESPONDING) {
            const esp_err_t error = event.sent == SendResult::SENT ? add_peer(peer_config(transaction)) : ESP_FAIL;
            if (error == ESP_OK) {
                phase = Phase::WAIT_CONFIRM;
                phase_deadline = after(TRANSACTION_WAIT_MS);
            } else {
                report(error);
                rollback_transaction();
                phase = Phase::LISTENING;
            }
            if (error == ESP_OK && confirm_pending) {
                Event confirm = {};
                confirm.source = transaction.peer;
                confirm.nonce = transaction.nonce;
                confirm_pending = false;
                accept_confirm(confirm);
            }
        } else if (event.type == EventType::CONFIRM_SENT && phase == Phase::CONFIRM) {
            if (event.sent != SendResult::ACKNOWLEDGED) {
                scan_error = ESP_ERR_TIMEOUT;
                next_channel();
            } else if (transaction.extended) {
                phase = Phase::RESULT;
                request_attempt = 0;
                phase_deadline = after(RESPONSE_WAIT_MS);
            } else {
                commit_initiator();
            }
        } else if (event.type == EventType::RESULT_SENT && phase == Phase::SEND_RESULT) {
            finish(transaction.committed ? (event.sent == SendResult::ACKNOWLEDGED ? ESP_OK : ESP_ERR_TIMEOUT)
                                         : scan_error);
        }
        return;
    }
    if (event.message_id == MSG_CHANNEL_PROBE) {
        if (has_peer(event.source)) {
            send_nonce(event.source, MSG_CHANNEL_PROBE_RESPONSE, event.nonce, plain_options());
        }
        return;
    }
    if (event.message_id == MSG_PAIR_RESULT_QUERY && completed.nonce != 0 && completed.peer == event.source &&
        completed.nonce == event.nonce && has_peer(event.source)) {
        send_result(completed, ESP_OK, false);
        return;
    }
    switch (event.message_id) {
    case MSG_DISCOVERY_PING:
        if (phase == Phase::LISTENING) {
            suspend_peer(event.source);
            discovery_response(event);
        } else if (phase == Phase::WAIT_CONFIRM) {
            discovery_response(event);
        }
        break;
    case MSG_DISCOVERY_RESPONSE:
    case MSG_DISCOVERY_INFO:
        if (phase == Phase::DISCOVERY) {
            collect_candidate(event);
        }
        break;
    case MSG_PAIR_REQUEST:
    case MSG_PAIR_REQUEST_V2:
        if (session_active.load() && !initiator) {
            respond_pair(event);
        }
        break;
    case MSG_PAIR_RESPONSE:
    case MSG_PAIR_RESPONSE_V2:
        if (phase == Phase::RESPONSE) {
            accept_response(event);
        }
        break;
    case MSG_PAIR_CONFIRM:
        if (phase == Phase::RESPONDING && same_transaction(event)) {
            confirm_pending = true;
        } else {
            accept_confirm(event);
        }
        break;
    case MSG_PAIR_RESULT:
        if (same_transaction(event) &&
            (phase == Phase::CONFIRM || phase == Phase::RESULT || phase == Phase::RESPONSE)) {
            if (event.error == ESP_OK && phase != Phase::RESPONSE) {
                commit_initiator();
            } else if (event.error != ESP_OK) {
                finish(event.error);
            }
        }
        break;
    default:
        break;
    }
}
void tick() {
    if (phase == Phase::IDLE || phase == Phase::RECOVERY) {
        return;
    }
    if (!is_active()) {
        finish(ESP_ERR_INVALID_STATE);
        return;
    }
    if (!unlimited_window && elapsed(window_deadline)) {
        finish(initiator ? (scan_error == ESP_OK ? ESP_ERR_TIMEOUT : scan_error) : ESP_ERR_TIMEOUT);
        return;
    }
    if (phase == Phase::LISTENING || !elapsed(phase_deadline)) {
        return;
    }
    switch (phase) {
    case Phase::DISCOVERY:
        choose_candidate();
        break;
    case Phase::RESPONSE:
        if (++request_attempt <= 3) {
            request_pair();
            phase_deadline = after(RESPONSE_WAIT_MS);
        } else {
            scan_error = ESP_ERR_TIMEOUT;
            next_channel();
        }
        break;
    case Phase::CONFIRM:
        scan_error = ESP_ERR_TIMEOUT;
        next_channel();
        break;
    case Phase::RESULT:
        if (++request_attempt <= 3) {
            send_nonce(transaction.peer, MSG_PAIR_RESULT_QUERY, transaction.nonce, reliable_options());
            phase_deadline = after(RESPONSE_WAIT_MS);
        } else {
            scan_error = ESP_ERR_TIMEOUT;
            next_channel();
        }
        break;
    case Phase::RESPONDING:
    case Phase::WAIT_CONFIRM:
        report(ESP_ERR_TIMEOUT);
        rollback_transaction();
        phase = Phase::LISTENING;
        break;
    case Phase::SEND_RESULT:
        finish(transaction.committed ? ESP_ERR_TIMEOUT : scan_error);
        break;
    default:
        break;
    }
}
void pairing_task(void *) {
    Event event = {};
    while (true) {
        if (cancel_requested.load()) {
            LifecycleGuard guard;
            if (session_active.load()) {
                finish(ESP_ERR_INVALID_STATE);
            }
            phase = Phase::IDLE;
            recovery_active.store(false);
            xQueueReset(events);
            cancel_requested.store(false);
        }
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(10)) == pdTRUE && !cancel_requested.load()) {
            // 恢复探测等待期间 Link 必须能继续收发；其余状态转移与驱动生命周期串行。
            if (event.type == EventType::RECOVER) {
                process_event(event);
            } else {
                LifecycleGuard guard;
                if (!cancel_requested.load()) {
                    process_event(event);
                }
            }
        }
        {
            LifecycleGuard guard;
            if (!cancel_requested.load()) {
                tick();
            }
        }
    }
}

} // namespace

esp_err_t init_pairing() {
    if (initialized_pairing.load()) {
        return ESP_OK;
    }
    events = xQueueCreate(24, sizeof(Event));
    if (events == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    restore_peers();
    for (uint16_t id : HANDLER_IDS) {
        const esp_err_t error = register_handler(id, on_message);
        if (error != ESP_OK) {
            deinit_pairing();
            return error;
        }
    }
    if (xTaskCreate(pairing_task, "espnow_pair", 4096, nullptr, 3, &worker) != pdPASS) {
        deinit_pairing();
        return ESP_ERR_NO_MEM;
    }
    initialized_pairing.store(true);
    return ESP_OK;
}
void deinit_pairing() {
    initialized_pairing.store(false);
    LifecycleGuard guard;
    for (uint16_t id : HANDLER_IDS) {
        unregister_handler(id);
    }
    if (worker != nullptr) {
        vTaskDelete(worker);
        worker = nullptr;
    }
    rollback_transaction();
    if (events != nullptr) {
        vQueueDelete(events);
        events = nullptr;
    }
    completed = {};
    phase = Phase::IDLE;
    config = {};
    peers_suspended = false;
    session_active.store(false);
    recovery_active.store(false);
    cancel_requested.store(false);
}

} // namespace Internal

esp_err_t configure_pairing(const PairingConfig &policy) {
    using namespace Internal;
    LifecycleGuard guard;
    if (policy.local_role >= 32 || policy.peer_limit == 0 || policy.peer_limit > MAX_SAVED_PEERS ||
        (policy.replace_existing && policy.peer_limit != 1) || policy.scan_timeout_ms < 1000 ||
        policy.scan_timeout_ms > 60000) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!initialized_pairing.load() || session_active.load() || recovery_active.load() || cancel_requested.load()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = policy.peer_limit == 1 ? retain_first_peer() : ESP_OK;
    if (error == ESP_OK) {
        config = policy;
    }
    return error;
}
void get_pairing_result(PairingResult *result) {
    if (result == nullptr) {
        return;
    }
    portENTER_CRITICAL(&Internal::result_lock);
    *result = Internal::last_result;
    portEXIT_CRITICAL(&Internal::result_lock);
}
esp_err_t enter_pairing_mode(uint32_t timeout_ms) {
    using namespace Internal;
    LifecycleGuard guard;
    bool expected = false;
    if (!initialized_pairing.load() || !is_active() || recovery_active.load() || cancel_requested.load() ||
        !session_active.compare_exchange_strong(expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    Event event = {};
    event.type = EventType::LISTEN;
    event.timeout_ms = timeout_ms;
    if (xQueueSend(events, &event, 0) == pdTRUE) {
        return ESP_OK;
    }
    session_active.store(false);
    return ESP_ERR_NO_MEM;
}
void leave_pairing_mode() {
    if (!Internal::initialized_pairing.load()) {
        return;
    }
    Internal::cancel_requested.store(true);
    // Link 回调不得阻塞等待配对任务；普通 UI/Shell 调用等待清理后可立即 clear/start。
    if (xTaskGetCurrentTaskHandle() == Internal::task_handle) {
        return;
    }
    while (Internal::cancel_requested.load() || Internal::session_active.load() || Internal::recovery_active.load()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
bool is_pairing() { return Internal::session_active.load() || Internal::cancel_requested.load(); }
esp_err_t start_pairing() {
    using namespace Internal;
    LifecycleGuard guard;
    bool expected = false;
    if (!initialized_pairing.load() || !is_active() || recovery_active.load() || cancel_requested.load() ||
        !session_active.compare_exchange_strong(expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    Event event = {};
    event.type = EventType::START;
    if (xQueueSend(events, &event, 0) == pdTRUE) {
        return ESP_OK;
    }
    session_active.store(false);
    return ESP_ERR_NO_MEM;
}
esp_err_t recover_peer_channel(const MacAddress &peer) {
    using namespace Internal;
    LifecycleGuard guard;
    bool expected = false;
    if (!initialized_pairing.load() || !is_active() || session_active.load() || cancel_requested.load() ||
        !has_peer(peer) || !recovery_active.compare_exchange_strong(expected, true)) {
        return ESP_ERR_INVALID_STATE;
    }
    recovery_result.store(ESP_ERR_INVALID_STATE);
    Event event = {};
    event.type = EventType::RECOVER;
    event.source = peer;
    if (xQueueSend(events, &event, 0) == pdTRUE) {
        return ESP_OK;
    }
    recovery_active.store(false);
    return ESP_ERR_NO_MEM;
}
bool is_recovering_channel() { return Internal::recovery_active.load(); }
esp_err_t get_channel_recovery_result() { return Internal::recovery_result.load(); }
size_t get_saved_peer_count() { return Internal::saved_peer_count(); }
esp_err_t get_saved_peer(size_t index, SavedPeer *peer) { return Internal::read_saved_peer(index, peer); }
esp_err_t set_saved_peer_role(const MacAddress &address, uint8_t role) {
    Internal::LifecycleGuard guard;
    if (role >= 32) {
        return ESP_ERR_INVALID_ARG;
    }
    if (is_pairing() || is_recovering_channel()) {
        return ESP_ERR_INVALID_STATE;
    }
    const Internal::PeerStore store = Internal::load_store();
    for (const auto &stored : store.peers) {
        if (!stored.used || memcmp(stored.mac, address.bytes, MAC_ADDRESS_SIZE) != 0) {
            continue;
        }
        if (stored.reserved_role == role) {
            return ESP_OK;
        }
        PeerConfig peer = {};
        peer.address = address;
        peer.channel = stored.last_channel;
        peer.encrypted = true;
        memcpy(peer.lmk, stored.lmk, KEY_SIZE);
        return Internal::save_peer(peer, stored.last_channel, role);
    }
    return ESP_ERR_NOT_FOUND;
}
esp_err_t remove_saved_peer(const MacAddress &address) {
    Internal::LifecycleGuard guard;
    if (is_pairing() || is_recovering_channel()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t error = Internal::erase_peer(address);
    if (error == ESP_OK) {
        remove_peer(address);
    }
    return error;
}
esp_err_t clear_saved_peers() {
    Internal::LifecycleGuard guard;
    if (is_pairing() || is_recovering_channel()) {
        return ESP_ERR_INVALID_STATE;
    }
    const Internal::PeerStore old = Internal::load_store();
    const esp_err_t error = Internal::erase_all_peers();
    if (error == ESP_OK) {
        for (const auto &stored : old.peers) {
            if (!stored.used) {
                continue;
            }
            MacAddress address = {};
            memcpy(address.bytes, stored.mac, MAC_ADDRESS_SIZE);
            remove_peer(address);
        }
    }
    return error;
}

} // namespace EspNowLink
