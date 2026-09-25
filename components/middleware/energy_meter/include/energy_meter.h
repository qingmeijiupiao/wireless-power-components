/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 电量积分管理
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:37:26
 */
#ifndef ENERGY_METER_H
#define ENERGY_METER_H

#include <cstdint>

namespace EnergyMeter {

/**
 * @brief 共享计量会话快照。
 */
struct Snapshot {
    int64_t  charge_uah    = 0; /**< 相对当前基线的累计电量，单位 μAh。 */
    int64_t  energy_uwh    = 0; /**< 相对当前基线的累计能量，单位 μWh。 */
    uint64_t meter_time_ms = 0; /**< 当前计量会话持续时间，单位 ms。 */
};

/**
 * @brief 获取当前共享计量会话快照。
 * @return 相对计量值和会话持续时间。
 */
Snapshot snapshot();

/** @brief 获取 LP Core 自启动以来的精确累计值。 */
Snapshot lifetime_snapshot();

/** @brief 更新来自 LP Core 的精确累计值。 */
void update_lifetime(int64_t charge_uah, int64_t energy_uwh);

/**
 * @brief 将当前 LP Core 累计值设置为新的共享计量基线。
 *
 * 该操作不会修改 LP Core 自启动以来持续累加的底层计数器。
 */
void reset();

} // namespace EnergyMeter

#endif
