# diagnostic_log

基于 `ESP_LOG` 的持久化诊断事件标记。组件只定义日志语义，不依赖黑匣子；
应用层 `blackbox_service` 通过全局 `vprintf` Hook 识别标记并决定持久化策略。

## 事件接口

| 接口 | ESP 日志等级 | 黑匣子文本 | 强制状态快照 |
|---|---|---|---|
| `DEVICE_EVENT_I` | Info | 是 | 否 |
| `DEVICE_STATE_I` | Info | 是 | 是 |
| `DEVICE_EVENT_W` | Warning | 是 | 是 |
| `DEVICE_STATE_W` | Warning | 是 | 是 |
| `DEVICE_EVENT_E` | Error | 是 | 是 |
| `DEVICE_STATE_E` | Error | 是 | 是 |

所有普通 `ESP_LOGW` 和 `ESP_LOGE` 也由 Hook 自动保存文本，并且每条日志都强制
追加一份状态快照。普通 `ESP_LOGI` 只输出到运行日志，不进入黑匣子。

`EVENT` 与 `STATE` 对 Warning/Error 的落盘行为相同，保留两个名称是为了让调用处
明确表达事件是否代表状态变化，并为以后扩展协议保留语义。

## 格式约定

持久化事件使用稳定的键值格式：

```text
domain: event key=value key=value
```

状态变化应包含 `old`、`new`、`source`、`result` 或 `reason` 中适用的字段。禁止记录
密码、密钥和认证令牌。

## 依赖导航

无工程内组件依赖；仅依赖 ESP-IDF `log` 组件。
