# espnow_remote

按钮与急停共用的 Wireless Power 遥控客户端，迁自按钮工程，公开 API 与事务流程保持不变。

- 查找已配对控制器、恢复信道、提交开关请求、等待链路 ACK 和业务响应。
- 接收功率计数据响应，支持维护诊断命令。
- 控制事务结束后通过公共 `battery_level` 获取电量，尽力上报；组件日志使用 `ESP_LOGx`，产品黑匣子记录由应用负责。
- 依赖 `espnow_service_remote`，头文件仍为 `espnow_service.h`，命名空间仍为 `EspNowService`。

这是系列内业务组件，不是任意设备的通用 ESP-NOW 客户端。产品保留按钮/急停策略、UI、电池采样、启动与休眠决策。

沿用原实现的限制：请求状态为单例，调用方应串行执行控制/读取事务；阻塞接口不能在链路回调中调用。急停的关闭优先、取消旧开启请求、持续重试以及仪表 UI 数据订阅尚未在本轮实现。

配对配置为单目标，成功确认新目标后才替换旧记录；`start_pairing(clear_first)` 保留兼容参数，失败不会预先删除旧绑定。扫描期间控制/读取接口返回 `ESP_ERR_INVALID_STATE`；响应同时校验 request_id 与对端 MAC。`print_status()` 的格式化输出用于显式 Shell 查询。

使用者：`Wireless_power_switch_button`。`Wireless_power_emergency_stop` 使用同一公共 Link/协议组件，并保留自己的异步关闭优先、发送取消、触点和 UI 状态机。
