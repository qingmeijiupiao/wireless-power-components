#include "remote_switch_registry.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace EspNowService::RemoteRegistry {
namespace {
constexpr size_t MAX_REMOTES = 3;
constexpr int64_t HEARTBEAT_TIMEOUT_US = 3000000;
constexpr int64_t DEFAULT_INTERLOCK_TIMEOUT_US = 5000000;
constexpr char TAG[] = "RemoteRegistry";
struct Entry {
    bool used = false;
    RemoteSwitchStatus status = {};
    bool interlock_known = false;
    bool inhibited = true;
    int64_t interlock_at_us = 0;
};
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Entry entries[MAX_REMOTES] = {};
int64_t interlock_timeout_us = DEFAULT_INTERLOCK_TIMEOUT_US;

Entry *find(const EspNowLink::MacAddress &peer) {
    for (auto &entry : entries) {
        if (entry.used && entry.status.address == peer) {
            return &entry;
        }
    }
    return nullptr;
}
bool recent(int64_t timestamp, int64_t now) { return timestamp > 0 && now - timestamp < HEARTBEAT_TIMEOUT_US; }
bool within(int64_t timestamp, int64_t now, int64_t window) { return timestamp > 0 && now - timestamp < window; }
} // namespace

void refresh_bindings() {
    EspNowLink::SavedPeer saved[MAX_REMOTES] = {};
    size_t count = 0;
    while (count < MAX_REMOTES && EspNowLink::get_saved_peer(count, &saved[count]) == ESP_OK) {
        ++count;
    }
    portENTER_CRITICAL(&lock);
    Entry refreshed[MAX_REMOTES] = {};
    for (size_t i = 0; i < count; ++i) {
        Entry *previous = find(saved[i].address);
        if (previous != nullptr) {
            refreshed[i] = *previous;
        }
        refreshed[i].used = true;
        refreshed[i].status.address = saved[i].address;
        if (saved[i].role != 0 || previous == nullptr) {
            refreshed[i].status.role = saved[i].role;
        }
    }
    for (size_t i = 0; i < MAX_REMOTES; ++i) {
        entries[i] = refreshed[i];
    }
    portEXIT_CRITICAL(&lock);
}
void observe(const EspNowLink::MacAddress &peer, int battery_percent) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    Entry *entry = find(peer);
    if (entry != nullptr) {
        entry->status.connected = true;
        entry->status.last_seen_us = now;
        if (battery_percent >= 0 && battery_percent <= 100) {
            entry->status.battery_percent = static_cast<uint8_t>(battery_percent);
            entry->status.battery_valid = true;
        }
    }
    portEXIT_CRITICAL(&lock);
}
void set_interlock(const EspNowLink::MacAddress &peer, bool inhibited) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    Entry *entry = find(peer);
    if (entry != nullptr &&
        (entry->status.role == 0 || entry->status.role == static_cast<uint8_t>(PairingRole::EMERGENCY_STOP))) {
        entry->status.role = static_cast<uint8_t>(PairingRole::EMERGENCY_STOP);
        entry->status.connected = true;
        entry->status.last_seen_us = now;
        entry->interlock_known = true;
        entry->inhibited = inhibited;
        entry->interlock_at_us = now;
    }
    portEXIT_CRITICAL(&lock);
}
void persist_roles() {
    EspNowLink::MacAddress peers[MAX_REMOTES] = {};
    size_t count = 0;
    portENTER_CRITICAL(&lock);
    for (const auto &entry : entries) {
        if (entry.used && entry.interlock_known) {
            peers[count++] = entry.status.address;
        }
    }
    portEXIT_CRITICAL(&lock);
    for (size_t i = 0; i < count; ++i) {
        const esp_err_t error =
            EspNowLink::set_saved_peer_role(peers[i], static_cast<uint8_t>(PairingRole::EMERGENCY_STOP));
        if (error != ESP_OK && error != ESP_ERR_INVALID_STATE && error != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "persist role failed: %s", esp_err_to_name(error));
        }
    }
}
void set_interlock_timeout_ms(uint32_t timeout_ms) {
    portENTER_CRITICAL(&lock);
    interlock_timeout_us = timeout_ms == 0 ? DEFAULT_INTERLOCK_TIMEOUT_US : static_cast<int64_t>(timeout_ms) * 1000;
    portEXIT_CRITICAL(&lock);
}
bool is_inhibited() {
    const int64_t now = esp_timer_get_time();
    bool inhibited = false;
    portENTER_CRITICAL(&lock);
    for (const auto &entry : entries) {
        // 仅在急停已识别、正在请求禁止开启且心跳未超时时阻止；失联/休眠超过时限即放行。
        if (entry.used && entry.status.role == static_cast<uint8_t>(PairingRole::EMERGENCY_STOP) &&
            entry.interlock_known && entry.inhibited && within(entry.interlock_at_us, now, interlock_timeout_us)) {
            inhibited = true;
            break;
        }
    }
    portEXIT_CRITICAL(&lock);
    return inhibited;
}
bool get(const EspNowLink::MacAddress &peer, RemoteSwitchStatus &status) {
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    const Entry *entry = find(peer);
    status = entry != nullptr ? entry->status : RemoteSwitchStatus{};
    portEXIT_CRITICAL(&lock);
    status.connected = recent(status.last_seen_us, now);
    return entry != nullptr;
}
bool latest(RemoteSwitchStatus &status) {
    const int64_t now = esp_timer_get_time();
    status = {};
    portENTER_CRITICAL(&lock);
    for (const auto &entry : entries) {
        if (entry.used && entry.status.last_seen_us > status.last_seen_us) {
            status = entry.status;
        }
    }
    portEXIT_CRITICAL(&lock);
    status.connected = recent(status.last_seen_us, now);
    return status.connected;
}

} // namespace EspNowService::RemoteRegistry
