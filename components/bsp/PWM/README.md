# PWM

LEDC PWM 输出封装组件，提供自动通道分配、占空比百分比设置和析构释放能力。

## 模块特点

- **自动分配通道**：最多分配 4 路 LEDC low speed 通道
- **通道独占管理**：通过静态位图记录已使用通道，析构时自动释放
- **默认配置简洁**：默认频率 5 kHz，占空比分辨率 13 bit
- **百分比接口**：使用 0.0~100.0 的占空比百分比读写，设置时自动限幅

## 集成与使用

```cpp
#include "pwm.h"

pwm_t backlight;
ESP_ERROR_CHECK(backlight.init(GPIO_NUM_23));
ESP_ERROR_CHECK(backlight.set_duty_percent(30.0f));

float duty = backlight.get_duty_percent();
```

## API 参考

| API | 说明 |
|-----|------|
| `init(gpio, freq_hz, duty_resolution)` | 初始化 PWM 输出并自动分配 LEDC 通道和 timer |
| `get_duty_percent()` | 返回当前占空比百分比，未初始化时返回 `0.0f` |
| `set_duty_percent(percent)` | 设置占空比百分比，范围外自动限幅 |
| `~pwm_t()` | 停止 PWM 输出并释放通道 |

## 默认参数

| 参数 | 默认值 |
|------|--------|
| `freq_hz` | `5000` |
| `duty_resolution` | `LEDC_TIMER_13_BIT` |
| LEDC speed mode | `LEDC_LOW_SPEED_MODE` |
| 最大实例数 | 4 |

## 注意事项

- `init()` 已成功后再次调用会直接返回 `ESP_OK`，不会重新配置通道。
- 可用通道耗尽时返回 `ESP_ERR_NOT_FOUND`。
- `set_duty_percent()` 在未初始化时返回 `ESP_ERR_INVALID_STATE`。

## 环境与依赖

- **软件**：ESP-IDF v6.0+

<!-- dependency-links:start -->
## 依赖导航

无工程内组件依赖；仅依赖 ESP-IDF 组件或 C/C++ 标准库。

> 本节按当前 `CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 维护。
<!-- dependency-links:end -->
