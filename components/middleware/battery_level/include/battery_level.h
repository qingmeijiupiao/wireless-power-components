#ifndef BATTERY_LEVEL_H
#define BATTERY_LEVEL_H

#include <cstdint>

namespace BatteryLevel {

struct Status {
    int voltage_mv;
    uint8_t estimated_percent;
    uint8_t displayed_percent;
    bool charging;
    bool restored_from_rtc;
};

/**
 * @brief 根据已校准电压更新电量。
 *
 * 非充电状态下显示电量只减不增，充电状态下只增不减，结果始终限制在
 * 0~100。显示值会写入 RTC 保持内存的三个冗余副本。
 */
Status update(int voltage_mv, bool charging);

/** @brief 返回最近一次有效状态；尚未更新时返回 false。 */
bool get_status(Status& status);

/** @brief 清除运行期和 RTC 冗余电量状态，下一次 update() 重新估算。 */
void reset();

} // namespace BatteryLevel

#endif
