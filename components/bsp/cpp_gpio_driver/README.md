# cpp_gpio_driver

编译期泛型 GPIO 驱动，通过模板参数将引脚号与模式固化到类型中，零运行时开销封装 ESP-IDF `gpio_config` / `gpio_set_level` / `gpio_get_level`。

## 模块特点

- **编译期绑定**：`CppGpioDriver<GPIO_NUM_5, GpioMode::OUTPUT>` 将引脚和模式编码到类型
- **四种模式**：`INPUT`、`OUTPUT`、`INPUT_PULLUP`、`INPUT_PULLDOWN`
- **类型安全**：`set()` 在非 OUTPUT 模式下返回 `ESP_FAIL`

## 类结构与运行流程

```mermaid
classDiagram
    class GpioMode {
        <<enumeration>>
        INPUT
        OUTPUT
        INPUT_PULLUP
        INPUT_PULLDOWN
    }
    class CppGpioDriver~GPIO, Mode~ {
        -gpio_num_t gpio_num_
        -std::function~void(bool)~ on_change_callback_
        +init() esp_err_t
        +init(gpio_num_t gpio_num) esp_err_t
        +set(bool value) esp_err_t
        +get() bool
        +set_on_change_callback(callback) void
    }
    CppGpioDriver --> GpioMode
```

```mermaid
flowchart TD
    A["init() / init(gpio_num)"] --> B["生成 gpio_config_t"]
    B --> C{"Mode"}
    C -->|OUTPUT| D["GPIO_MODE_OUTPUT"]
    C -->|INPUT| E["GPIO_MODE_INPUT"]
    C -->|INPUT_PULLUP| F["GPIO_MODE_INPUT + pull_up"]
    C -->|INPUT_PULLDOWN| G["GPIO_MODE_INPUT + pull_down"]
    D --> H["gpio_config()"]
    E --> H
    F --> H
    G --> H
    H --> I["set()/get() 封装 ESP-IDF GPIO API"]
    I --> J["set() 成功后触发 on_change_callback"]
```

## 集成与使用

```cpp
#include "cpp_gpio_driver.hpp"

CppGpioDriver<GPIO_NUM_5, GpioMode::OUTPUT> relay;
relay.init();
relay.set(true);

CppGpioDriver<GPIO_NUM_4, GpioMode::INPUT_PULLUP> btn;
btn.init();
bool pressed = btn.get();
```

## API 参考

| 方法 | 说明 |
|------|------|
| `init()` | 配置 GPIO（模式、上下拉） |
| `set(bool)` | 输出高/低电平，仅 OUTPUT 模式有效 |
| `get() const` | 读取输入电平 |

## 环境与依赖

- **软件**：ESP-IDF v6.0+、C++20

<!-- dependency-links:start -->
## 依赖导航

无工程内组件依赖；仅依赖 ESP-IDF 组件或 C/C++ 标准库。

> 本节按当前 `CMakeLists.txt` 的 `REQUIRES` / `PRIV_REQUIRES` 维护。
<!-- dependency-links:end -->
