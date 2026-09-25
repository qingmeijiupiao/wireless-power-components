#include "energy_meter.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

namespace EnergyMeter {
namespace {

portMUX_TYPE lock                = portMUX_INITIALIZER_UNLOCKED;
int64_t      charge_uah_baseline = 0;
int64_t      energy_uwh_baseline = 0;
// 保留 LP Core 的精确累计值，避免展示用 float 参与基线差分后损失低位精度。
int64_t      lifetime_charge_uah = 0;
int64_t      lifetime_energy_uwh = 0;
int64_t      meter_start_us      = 0;

} // namespace

Snapshot snapshot() {
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&lock);
    Snapshot result = {
        .charge_uah    = lifetime_charge_uah - charge_uah_baseline,
        .energy_uwh    = lifetime_energy_uwh - energy_uwh_baseline,
        .meter_time_ms = static_cast<uint64_t>((now_us - meter_start_us) / 1000),
    };
    portEXIT_CRITICAL(&lock);
    return result;
}

Snapshot lifetime_snapshot() {
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&lock);
    Snapshot result = {
        .charge_uah    = lifetime_charge_uah,
        .energy_uwh    = lifetime_energy_uwh,
        .meter_time_ms = static_cast<uint64_t>(now_us / 1000),
    };
    portEXIT_CRITICAL(&lock);
    return result;
}

void update_lifetime(int64_t charge_uah, int64_t energy_uwh) {
    portENTER_CRITICAL(&lock);
    lifetime_charge_uah = charge_uah;
    lifetime_energy_uwh = energy_uwh;
    portEXIT_CRITICAL(&lock);
}

void reset() {
    const int64_t now_us = esp_timer_get_time();

    portENTER_CRITICAL(&lock);
    charge_uah_baseline = lifetime_charge_uah;
    energy_uwh_baseline = lifetime_energy_uwh;
    meter_start_us      = now_us;
    portEXIT_CRITICAL(&lock);
}

} // namespace EnergyMeter
