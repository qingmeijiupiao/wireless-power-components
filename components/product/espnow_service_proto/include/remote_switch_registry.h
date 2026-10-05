#pragma once

#include "espnow_service_proto.h"

namespace EspNowService::RemoteRegistry {

/** @brief 从已保存绑定同步三槽状态表；任务上下文，配对删除后清理孤立记录。 */
void refresh_bindings();
/** @brief 记录已绑定开关的活动/电量；不会让未配对来源占用槽位。 */
void observe(const EspNowLink::MacAddress &peer, int battery_percent = -1);
/** @brief 更新急停禁止开启状态；旧记录角色由维护任务持久化升级。 */
void set_interlock(const EspNowLink::MacAddress &peer, bool inhibited);
/** @brief 维护任务持久化已识别的急停角色，不在接收回调写 NVS。 */
void persist_roles();
/** @brief 已绑定急停未知、禁止开启或心跳超过 3 秒时返回 true；短临界区。 */
bool is_inhibited();
/** @brief 查询指定已绑定开关；connected 表示最近 3 秒内收到活动。 */
bool get(const EspNowLink::MacAddress &peer, RemoteSwitchStatus &status);
/** @brief 兼容旧单状态展示接口，返回最近活动的开关。 */
bool latest(RemoteSwitchStatus &status);

} // namespace EspNowService::RemoteRegistry
