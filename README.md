# 无线电源系列共用组件

本仓库用于统一维护无线功率计 Lite、无线功率计 Pro V2、无线开关按键工程和无线急停工程共用的 ESP-IDF 组件。

## 当前状态

- 第一阶段（`e73b43e`）：迁入 Lite 与 Pro 工程中整目录一致、无需修改组件文件的部分。
- 第二阶段：迁入三个工程间功能与 API 一致、仅存在格式差异或缺陷修复差异的非 APP 组件，并以 Lite/Pro 中较新的实现作为统一版本；`shell` 的提示符改为运行时可配置以便各工程共用。
- `can_resistor` 本质是"带 NVS 持久化的输出 GPIO"，已泛化为通用的 `nvs_gpio_output`；CAN 终端电阻等具体外设语义下沉到各工程的板级门面。
- 开关工程已接入本批中与硬件无关的组件（`HXC_NVS`、`PWM`、`circular_flash_buffer`、`Interp`、`blackbox`、`ADC`、`wifi_manager`、`shell`）。本轮进一步统一了 `espnow_link`，`Temperature` 仍保留在产品工程。

## 目录规划

每个组件保留独立的 `CMakeLists.txt`，需要声明其他组件依赖时再添加 `idf_component.yml`：

```text
components/
  common/
  bsp/
  middleware/
  product/                    产品系列共享的业务组件
```

各固件工程通过 ESP-IDF Component Manager 的 Git 依赖引用所需组件，并固定到本仓库的标签或提交。发布版本以整个仓库为单位打标签，各固件工程可分别决定何时升级。

### 组件清单

| 阶段 | 分类 | 组件 |
| --- | --- | --- |
| 第一阶段 | `common` | `diagnostic_log` |
| 第一阶段 | `bsp` | `HXC_TWAI`、`cpp_gpio_driver` |
| 第一阶段 | `middleware` | `DNSServer`、`energy_meter`、`time_service` |
| 第二阶段 | `common` | `Interp` |
| 第二阶段 | `bsp` | `HXC_NVS`、`PWM`、`circular_flash_buffer`、`ADC`、`wifi_manager`、`shell`、`nvs_gpio_output` |
| 第二阶段 | `middleware` | `blackbox`、`ota_manager`、`WebServer`、`Button`、`espnow_link` |

第二阶段的 `ADC`、`wifi_manager`、`WebServer`、`Button`、`espnow_link` 采用 Lite/Pro 中较新的实现（含并发串行化、Captive Portal DNS、探测回落、`PRESS` 事件等修复/扩展）。

涉及 ESP32-C3 按键设备的组件，会先核对硬件差异，并验证 ESP-NOW 协议、配对流程及持久化数据的兼容性。

## 上位机工具

| 工具 | 路径 | 说明 |
| --- | --- | --- |
| 黑匣子串口控制台 | [`tools/blackbox_console.html`](tools/blackbox_console.html) | 基于 Web Serial 的单文件网页：既可作为普通串口命令行，也能拉取黑匣子日志（最新 N 条 / 全部）并导出 txt |

使用方式：

1. 用 Chrome 或 Edge 打开 `tools/blackbox_console.html`。Web Serial 需要安全上下文；若直接以 `file://` 打开被拦截，可在仓库根目录执行 `python -m http.server`，再访问 `http://localhost:8000/tools/blackbox_console.html`。
2. 点击“连接串口”，选择设备串口，确认波特率（默认 `115200`）和行尾（默认 LF）。
3. “终端”页可直接输入任意 Shell 命令，也可使用工具栏快捷按钮（版本 / 状态 / 拉取100条 / 拉取全部 / 清空黑匣子）。
4. “黑匣子日志”页点击“拉取最新N条”或“拉取全部”，工具会发送 `blackbox dump <n|all>`，并按 `BLACKBOX_DUMP_BEGIN` / `BLACKBOX_DUMP_END` 解析成表格。
5. 支持“导出原始txt”和“导出解析txt”。

日志行协议见 [`components/middleware/blackbox_service/README.md`](components/middleware/blackbox_service/README.md)。

## 开源协议

本仓库采用 [MIT 协议](LICENSE)。

## 按钮与急停工程共用组件（2026-09-29）

只迁移至少两个工程会使用的组件。SH1106 驱动仍在急停工程，未迁入本仓库。

| 组件 | 位置 | 消费工程 |
|---|---|---|
| `battery_level` | `components/middleware/battery_level` | 按钮、急停 |
| `blackbox_service` | `components/middleware/blackbox_service` | 按钮、急停 |
| `espnow_remote` | `components/product/espnow_remote` | 按钮、急停 |
| `espnow_service_remote` | `components/product/espnow_service_remote` | 按钮、急停 |
| `espnow_link`（已有，统一引用） | `components/middleware/espnow_link` | 按钮、急停；Lite/Pro 按原有固定版本使用 |

- `product` 表示系列内业务复用，并非通用 ESP-NOW 协议。`espnow_service_remote` 保留 `EspNowService` 命名空间与头文件，但对输出请求返回 NOT_READY；不能替换功率计接收端的 `espnow_service`。
- 日志捕获由应用传入静态 INFO 标签表，WARN/ERROR 的捕获和内部标签排除行为保持不变。此版本仍要求 Log V1。
- 电量估算保留原有实测单节锂电曲线、显示单调策略及 RTC 三副本；不适用于任意电池。正式急停电池型号变化时须复核曲线。
- 链路的 `CONFIG_ESPNOW_LINK_TASK_STACK_SIZE` 默认 4096；按钮和急停配置为原按钮使用的 5120。帧编码、配对报文及 peer 存储布局没有改变。
- 两个消费工程通过 `main/idf_component.yml` 固定引用公共提交 `15525b7a2d3cc0694bbc0b65dcac8335cd73d454`；其他公共依赖仍固定在原 Git 提交。Component Manager 自动获取源码，独立 checkout 和 CI 不再依赖相邻本地仓库。
- 按钮手势、急停状态机、硬件引脚、电池采样/校准、休眠、状态灯和页面仍属于各产品。
- 急停当前只运行屏幕 bring-up；`-D ESTOP_VALIDATE_SHARED_REMOTE=ON` 可编译共享遥控组件，但不会启动无线控制。

本机回归：`python tests/run_shared_host_tests.py`，使用真实迁移源码验证日志筛选和固定协议字节；并非硬件/无线时序测试。
