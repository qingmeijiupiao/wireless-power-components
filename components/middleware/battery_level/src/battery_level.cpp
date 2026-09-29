#include "battery_level.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "Interp.hpp"
#include "battery_curve_generated.h"
#include "esp_attr.h"
#include "esp_crc.h"
#include "esp_system.h"

namespace BatteryLevel {
namespace {

constexpr uint32_t RTC_MAGIC = 0x424C564CU;
constexpr uint16_t RTC_VERSION = 1;
constexpr size_t RTC_COPY_COUNT = 3;

struct RtcRecord {
    uint32_t magic;
    uint16_t version;
    uint8_t displayed_percent;
    uint8_t charging;
    uint32_t sequence;
    uint32_t sequence_inverse;
    uint32_t crc32;
};

// RTC_NOINIT_ATTR 在 ESP32-C3 上位于深睡保持的 RTC 内存，且冷启动时不初始化。
RTC_NOINIT_ATTR RtcRecord rtc_records[RTC_COPY_COUNT];

bool initialized;
bool restored_from_rtc;
bool status_valid;
Status current_status = {};
uint32_t current_sequence;

uint32_t record_crc(const RtcRecord& record) {
    RtcRecord copy = record;
    copy.crc32 = 0;
    return esp_crc32_le(0, reinterpret_cast<const uint8_t*>(&copy), sizeof(copy));
}

bool record_valid(const RtcRecord& record) {
    return record.magic == RTC_MAGIC &&
           record.version == RTC_VERSION &&
           record.displayed_percent <= 100 &&
           record.charging <= 1 &&
           record.sequence_inverse == ~record.sequence &&
           record.crc32 == record_crc(record);
}

bool sequence_newer(uint32_t candidate, uint32_t reference) {
    // 有符号差值比较允许 32 位序号自然回绕，只要相邻有效记录跨度小于半周期。
    return static_cast<int32_t>(candidate - reference) > 0;
}

void restore_rtc_state() {
    initialized = true;
    const RtcRecord* newest = nullptr;
    // 三个副本分别校验，选择序号最新的有效记录；冷启动时随机内容会被校验拒绝。
    for (const RtcRecord& record : rtc_records) {
        if (!record_valid(record)) {
            continue;
        }
        if (newest == nullptr || sequence_newer(record.sequence, newest->sequence)) {
            newest = &record;
        }
    }
    if (newest == nullptr) {
        return;
    }

    current_sequence = newest->sequence;
    current_status.displayed_percent = newest->displayed_percent;
    current_status.charging = newest->charging != 0;
    restored_from_rtc = true;
    status_valid = true;
}

const NonEquidistantInterp<int, float>& curve() {
    static const NonEquidistantInterp<int, float> interpolation([] {
        std::vector<std::pair<int, float>> points;
        points.reserve(BatteryCurveGenerated::POINTS.size());
        for (const auto& point : BatteryCurveGenerated::POINTS) {
            points.emplace_back(point.voltage_mv,
                                static_cast<float>(point.percent));
        }
        return points;
    }());
    return interpolation;
}

uint8_t estimate_percent(int voltage_mv) {
    const float estimated = curve().interpolate(voltage_mv);
    const int rounded = static_cast<int>(estimated + 0.5F);
    return static_cast<uint8_t>(std::clamp(rounded, 0, 100));
}

void persist_rtc_state(uint8_t displayed_percent, bool charging) {
    ++current_sequence;
    RtcRecord record = {
        .magic = RTC_MAGIC,
        .version = RTC_VERSION,
        .displayed_percent = displayed_percent,
        .charging = static_cast<uint8_t>(charging ? 1 : 0),
        .sequence = current_sequence,
        .sequence_inverse = ~current_sequence,
        .crc32 = 0,
    };
    record.crc32 = record_crc(record);

    // 每次同步写入三个副本，任意一到两个副本损坏时仍可恢复当前状态。
    for (RtcRecord& copy : rtc_records) {
        copy = record;
    }
}

} // namespace

Status update(int voltage_mv, bool charging) {
    if (!initialized) {
        restore_rtc_state();
    }

    const uint8_t estimated_percent = estimate_percent(voltage_mv);
    uint8_t displayed_percent = estimated_percent;
    if (status_valid) {
        // 抑制负载波动造成的电量跳变：放电只减不增，充电只增不减。
        displayed_percent = charging
                                ? std::max(current_status.displayed_percent,
                                           estimated_percent)
                                : std::min(current_status.displayed_percent,
                                           estimated_percent);
    }

    current_status = {
        .voltage_mv = voltage_mv,
        .estimated_percent = estimated_percent,
        .displayed_percent = displayed_percent,
        .charging = charging,
        .restored_from_rtc = restored_from_rtc,
    };
    status_valid = true;
    persist_rtc_state(displayed_percent, charging);
    return current_status;
}

bool get_status(Status& status) {
    if (!initialized) {
        restore_rtc_state();
    }
    if (!status_valid) {
        return false;
    }
    status = current_status;
    return true;
}

void reset() {
    for (RtcRecord& record : rtc_records) {
        record = {};
    }
    initialized = true;
    restored_from_rtc = false;
    status_valid = false;
    current_status = {};
    current_sequence = 0;
}

} // namespace BatteryLevel
