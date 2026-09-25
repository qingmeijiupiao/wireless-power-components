# 无线电源系列共用组件

本仓库用于统一维护无线功率计 Lite、无线功率计 Pro V2 和无线开关按键工程共用的 ESP-IDF 组件。

## 当前状态

第一阶段已迁入 Lite 与 Pro 工程中整目录一致、无需修改组件文件的部分。按键工程尚未接入本仓库。

## 目录规划

每个组件保留独立的 `CMakeLists.txt`，需要声明其他组件依赖时再添加 `idf_component.yml`：

```text
components/
  common/
  bsp/
  middleware/
```

各固件工程通过 ESP-IDF Component Manager 的 Git 依赖引用所需组件，并固定到本仓库的标签或提交。发布版本以整个仓库为单位打标签，各固件工程可分别决定何时升级。

### 第一阶段组件

| 分类 | 组件 |
| --- | --- |
| `common` | `diagnostic_log` |
| `bsp` | `HXC_TWAI`、`cpp_gpio_driver` |
| `middleware` | `DNSServer`、`energy_meter`、`time_service` |

这些组件的文件与迁移前的 Lite、Pro 工程一致。`can_resistor` 虽然两工程中的文件也一致，但其文档链接和组件依赖指向尚未迁入的 `HXC_NVS`，留待后续阶段处理。

涉及 ESP32-C3 按键设备的组件，会先核对硬件差异，并验证 ESP-NOW 协议、配对流程及持久化数据的兼容性。

## 开源协议

本仓库采用 [MIT 协议](LICENSE)。
