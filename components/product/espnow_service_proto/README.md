# espnow_service_proto

Wireless Power 产品业务的**线上协议层**：消息 ID、`DeviceData` 快照、业务枚举、
响应/数据回调类型，以及固定偏移的小端 payload 编解码与传输语义校验。

遥控端（`espnow_service_remote`）和接收端（`espnow_service_meter`）都依赖本组件，
从而保证两端报文字段与长度完全一致，避免各工程重复实现 `protocol.cpp` 造成漂移。

## 内容

- `EspNowService`：`SwitchAction`、`SwitchResult`、`DeviceData`、`RemoteSwitchStatus`、
  `DEVICE_STATUS_OUTPUT_ON`、`SwitchResponseHandler`、`DataReceivedHandler`。
- `EspNowService::Internal`：消息 ID（`0x0200`、`0x0201`、`0x0202`、`0x0203`、`0x0210`、
  `0x0211`、`0x0212`）、固定长度常量、`SwitchRequest`/`SwitchResponse`/`DataMessage`、
  `encode_*` 编解码、`reliable_options()`、`is_reliable_unicast()`。

## 消息

| ID | 语义 |
|---|---|
| `0x0200` | 可靠开关控制请求 |
| `0x0201` | 可靠开关控制响应 |
| `0x0202` | 尽力远程开关电量上报 |
| `0x0203` | 可选详细拒绝原因与保护掩码（旧端忽略） |
| `0x0210` | 可靠实时数据请求 |
| `0x0211` | 可靠实时数据响应 |
| `0x0212` | 尽力周期数据上报 |

## 依赖

- `espnow_link`：提供 `MacAddress`、`Codec` 与 `SendOptions`。
