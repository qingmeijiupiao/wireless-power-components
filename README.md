# 无线电源系列共用组件

本仓库用于统一维护无线功率计 Lite、无线功率计 Pro V2 和无线开关按键工程共用的 ESP-IDF 组件。

## 当前状态

- 第一阶段（`e73b43e`）：迁入 Lite 与 Pro 工程中整目录一致、无需修改组件文件的部分。
- 第二阶段：迁入三个工程间功能与 API 一致、仅存在格式差异或缺陷修复差异的非 APP 组件，并以 Lite/Pro 中较新的实现作为统一版本；`shell` 的提示符改为运行时可配置以便各工程共用。
- 开关工程已接入本批中与硬件无关的组件（`HXC_NVS`、`PWM`、`circular_flash_buffer`、`Interp`、`blackbox`、`ADC`、`wifi_manager`、`shell`）。其 `espnow_link`、`Temperature` 存在协议或功能差异，暂未接入。

## 目录规划

每个组件保留独立的 `CMakeLists.txt`，需要声明其他组件依赖时再添加 `idf_component.yml`：

```text
components/
  common/
  bsp/
  middleware/
```

各固件工程通过 ESP-IDF Component Manager 的 Git 依赖引用所需组件，并固定到本仓库的标签或提交。发布版本以整个仓库为单位打标签，各固件工程可分别决定何时升级。

### 组件清单

| 阶段 | 分类 | 组件 |
| --- | --- | --- |
| 第一阶段 | `common` | `diagnostic_log` |
| 第一阶段 | `bsp` | `HXC_TWAI`、`cpp_gpio_driver` |
| 第一阶段 | `middleware` | `DNSServer`、`energy_meter`、`time_service` |
| 第二阶段 | `common` | `Interp` |
| 第二阶段 | `bsp` | `HXC_NVS`、`PWM`、`circular_flash_buffer`、`ADC`、`wifi_manager`、`shell` |
| 第二阶段 | `middleware` | `blackbox`、`can_resistor`、`ota_manager`、`WebServer`、`Button`、`espnow_link` |

第二阶段的 `ADC`、`wifi_manager`、`WebServer`、`Button`、`espnow_link` 采用 Lite/Pro 中较新的实现（含并发串行化、Captive Portal DNS、探测回落、`PRESS` 事件等修复/扩展）。

涉及 ESP32-C3 按键设备的组件，会先核对硬件差异，并验证 ESP-NOW 协议、配对流程及持久化数据的兼容性。

## 开源协议

本仓库采用 [MIT 协议](LICENSE)。
