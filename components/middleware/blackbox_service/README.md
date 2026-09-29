# blackbox_service

按钮与急停共用的日志捕获组件。在 `middleware/blackbox` 循环存储之上捕获
ESP-IDF 日志，并负责深睡前同步。

## 记录策略

- 捕获全部 `ESP_LOGW`、`ESP_LOGE`。
- `ESP_LOGI` 只保留产品关键 TAG，过滤 WiFi、PHY、协议栈和 Shell 初始化噪声。
- 所有记录均保存为字符串，不定义结构化日志协议。
- 落盘格式为 `[I][TAG] message`、`[W][TAG] message` 或 `[E][TAG] message`。
- 运行时消息使用 ASCII 英文，不向串口或 Flash 日志写入中文字符串。
- 排除黑匣子内部 TAG，防止 Flash 写入日志递归捕获。
- 每次启动由 `app_main` 调用 `app_runtime` 写入 `boot:` 记录，休眠前由
  `app_runtime` 写入 `sleep:` 记录。
- 统计日志捕获数、RAM 环丢弃数和持久化提交失败数。

启动阶段记录包含：

```text
boot: reset=<reason> wake=<source> usb=<0|1> button=<0|1>
      heap_free=<bytes> heap_min=<bytes>
firmware: version=<major.minor.patch> build=<time>
battery: voltage_mv=<mV> result=<err>
```

休眠记录包含：

```text
sleep: uptime_ms=<ms> battery_mv=<mV> battery_result=<err>
       heap_free=<bytes> heap_min=<bytes> records=<used>/<capacity>
       log_drop=<count> persist_fail=<count>
```

## 数据流

```mermaid
flowchart LR
    Log["ESP_LOG I/W/E"] --> Hook["vprintf hook"]
    Hook --> Ring["固定 64 条 RAM 环"]
    Ring --> Worker["blackbox_service task"]
    Wake["wake event"] --> MW["middleware/blackbox"]
    Worker --> MW
    MW --> Queue["异步 Flash 队列"]
    Queue --> Flash["blackbox 分区"]
```

日志钩子只做格式化和 RAM 入环，不直接操作 Flash。后台任务每 20 ms
排空一次，避免日志调用链阻塞按键和无线任务。

## 深睡同步

`BlackboxService::sync()` 先排空 RAM 捕获环，再等待 middleware 写队列屏障。
`AppRuntime::sleep_when_inputs_idle()` 在调用 `PowerManager::enter_deep_sleep()`
前执行该接口，确保短时唤醒产生的日志已经落盘。

## API

```cpp
ESP_ERROR_CHECK(Blackbox::init());
static constexpr const char* tags[] = {"AppRuntime", "EspNowRemote"};
ESP_ERROR_CHECK(BlackboxService::init({tags, sizeof(tags) / sizeof(tags[0])}));

BlackboxService::append_text_event("wake: source=button battery_mv=%d", battery_mv);
BlackboxService::sync();
```

## 约束

- 当前实现依赖 `CONFIG_LOG_VERSION_1=y`。
- C/C++ 运行时字符串应保持 ASCII；中文说明写在注释和 Markdown 文档中。
- 单条逻辑字符串最多 199 字符，超出部分由 middleware 截断。
- RAM 环满时新捕获日志会丢弃，业务日志调用不会等待 Flash。
- `get_statistics()` 可读取捕获、待处理、丢弃和持久化失败计数。
- `sync()` 仅用于导出和休眠边界，不应放入按键实时反馈路径。

## 环境与依赖

| 类别 | 要求 |
|------|------|
| 框架 | ESP-IDF v6.0+ |
| 组件 | `blackbox`, `freertos`, `log` |

INFO 标签数组和字符串必须具有整个运行期的生命周期。无参数 `init()` 默认只收 WARN/ERROR。初始化应在启动阶段单任务执行；重复调用不会覆盖已安装配置。启动/休眠事件格式由应用定义，本文中的 boot/sleep 仅为按钮工程示例。
