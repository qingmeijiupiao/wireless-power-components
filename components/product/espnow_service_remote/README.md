# espnow_service_remote

按钮与急停共用的遥控端协议实现。组件名改为 `espnow_service_remote`，公开头文件 `espnow_service.h` 和命名空间 `EspNowService` 保持不变。
此实现不控制功率输出，对合法控制请求返回 `NOT_READY`；不能替换 Lite/Pro V2 接收端的 `espnow_service`，也不能与其同时链接。
本轮只迁移源码与构建依赖，没有扩展保护原因或改变报文布局。

`espnow_service` 只实现 Wireless Power 产品业务协议：

- 开关控制请求与响应
- 遥控开关电量上报
- 实时数据读取请求与响应
- 周期数据上报
- 业务 payload 编解码

可靠收发、ACK、重传、去重、ESP-NOW 驱动回调、配对、peer/LMK 持久化、信道恢复和
消息 ID 分发全部由 `espnow_link` 负责。

## 结构

```text
espnow_service/
├── include/espnow_service.h
├── private_include/espnow_service_internal.h
└── src/
    ├── espnow_service_business.cpp
    └── espnow_service_business_protocol.cpp
```

`init()` 为每个业务消息 ID 直接向 `espnow_link` 注册独立回调，不创建额外业务队列或
二次分发任务。每个接收回调内部直接完成校验、解码和业务处理。回调由 `espnow_link`
消息分发任务调用，因此业务处理必须快速返回。

## 消息

| ID | 语义 |
|---|---|
| `0x0200` | 可靠开关控制请求 |
| `0x0201` | 可靠开关控制响应 |
| `0x0202` | 尽力远程开关电量上报 |
| `0x0210` | 可靠实时数据请求 |
| `0x0211` | 可靠实时数据响应 |
| `0x0212` | 尽力周期数据上报 |

请求和响应使用非零 `request_id` 关联。协议字段通过 `espnow_codec.h` 按明确的小端格式
读写，不使用可能产生未对齐访问、严格别名违规和本机端序依赖的 `reinterpret_cast`。

远程开关电量 payload 为 1 字节百分比，范围 `0..100`，使用已配对 peer 加密单播和
尽力传输，不等待 ACK。按键产品在每次控制包成功提交并结束控制事务后发送一次当前
`BatteryLevel::displayed_percent`。

## 初始化

```cpp
ESP_ERROR_CHECK(EspNowLink::init());
ESP_ERROR_CHECK(EspNowService::init());
```

按键产品在 `espnow_remote` 中注册控制响应和数据接收业务回调。

## 依赖

- `espnow_link`
