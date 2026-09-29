# battery_level

单节锂电池电量估算与显示策略组件。该组件不直接访问 ADC、USB GPIO 或
NVS，只接收 `battery_voltage` 输出的已校准电压和当前充电状态。

## 曲线来源

当前固件曲线由一次受控放电测试生成，测试曲线首点为 `4121 mV`，
生成时按比例映射到真实满电电压 `4200 mV`。原始 CSV 体积较大且不参与
固件构建，因此不纳入仓库；固件使用已生成并版本化的
`battery_curve_generated.h`：

```text
curve_voltage = measured_voltage * 4200 / 4121
```

在按钮工程根目录使用以下命令重新生成固件曲线：

```powershell
python scripts/generate_battery_curve_header.py `
    <battery_curve.csv> `
    ../wireless-power-components/components/middleware/battery_level/include/battery_curve_generated.h
```

生成器完成以下处理：

1. 按剩余电量 `0~100%` 每 1% 重采样。
2. 使用满电比例校正全部电压点。
3. 收敛 ADC 噪声、重复电压和局部回升，确保电压严格递增。
4. 输出 101 个编译期常量点。

运行时使用 `NonEquidistantInterp<int, float>` 进行线性插值，超出曲线范围
时自动限制到 `0%` 或 `100%`。

## 显示策略

`update(voltage_mv, charging)` 同时返回曲线估算值和显示值：

- 非充电状态：`displayed = min(previous, estimated)`，只减不增。
- 充电状态：`displayed = max(previous, estimated)`，只增不减。
- 首次启动或 RTC 数据无效：直接使用当前曲线估算值。
- 所有结果最终限制在 `0~100`。

USB 插拔只改变后续允许的变化方向，不会立即清空已有显示电量。

## RTC 冗余

显示电量保存在 `RTC_NOINIT_ATTR` 深睡保持区域。每次更新同步写入三个
副本，每份记录包含：

- magic 和结构版本；
- 显示电量与充电状态；
- 单调递增序号及其按位反码；
- 整条记录的 CRC32。

恢复时逐份校验并选择序号最新的有效记录。任意一到两个副本损坏时仍能
恢复；全部无效时退回当前电压曲线估算。

`battery reset-level` 可清除三个 RTC 副本，用于诊断和测试。

## API

```cpp
const BatteryLevel::Status status =
    BatteryLevel::update(voltage_mv, usb_connected);
```

组件依赖 `Interp`、`esp_common` 和 `esp_hw_support`。
