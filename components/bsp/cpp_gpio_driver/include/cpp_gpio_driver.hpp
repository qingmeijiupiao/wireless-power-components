/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: GPIO驱动类，用于操作GPIO引脚
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:35:53
 */
#ifndef CPP_GPIO_DRIVER_HPP
#define CPP_GPIO_DRIVER_HPP
#include <driver/gpio.h>
#include <functional>

enum class GpioMode { INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN };

template <gpio_num_t GPIO, GpioMode Mode> class CppGpioDriver {
  public:
    /** @brief `CppGpioDriver` 接口。 */
    CppGpioDriver() {}
    /** @brief `init` 接口。 */
    esp_err_t init() {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask  = (1ULL << GPIO);
        if (Mode == GpioMode::INPUT) {
            io_conf.mode = GPIO_MODE_INPUT;
        } else if (Mode == GpioMode::OUTPUT) {
            io_conf.mode = GPIO_MODE_OUTPUT;
        } else if (Mode == GpioMode::INPUT_PULLUP) {
            io_conf.mode       = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        } else if (Mode == GpioMode::INPUT_PULLDOWN) {
            io_conf.mode         = GPIO_MODE_INPUT;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
        io_conf.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&io_conf);
    }

    /** @brief `set` 接口。 */
    esp_err_t set(bool value) {
        if (Mode == GpioMode::OUTPUT) {
            output_value_ = value;
            gpio_set_level(GPIO, value);
            if (on_change_callback_ != nullptr) {
                on_change_callback_(value);
            }
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    /** @brief `get` 接口。 */
    bool get() const {
        if (Mode == GpioMode::OUTPUT) {
            return output_value_;
        }
        return gpio_get_level(GPIO);
    }

    /**
     * @brief : 设置引脚状态改变回调函数
     * @return  {*}
     * @param {function<void(bool)>} callback
     */
    void set_on_change_callback(std::function<void(bool)> callback) {
        on_change_callback_ = callback;
    }

  private:
    std::function<void(bool)> on_change_callback_ = nullptr;
    bool                      output_value_       = false;
};

template <GpioMode Mode> class CppGpioDriver<GPIO_NUM_NC, Mode> {
    gpio_num_t gpio_num_ = GPIO_NUM_NC;

  public:
    /** @brief `CppGpioDriver` 接口。 */
    CppGpioDriver() = default;

    /** @brief `init` 接口。 */
    esp_err_t init(gpio_num_t gpio_num) {
        gpio_num_             = gpio_num;
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask  = (1ULL << gpio_num_);
        if (Mode == GpioMode::INPUT) {
            io_conf.mode = GPIO_MODE_INPUT;
        } else if (Mode == GpioMode::OUTPUT) {
            io_conf.mode = GPIO_MODE_OUTPUT;
        } else if (Mode == GpioMode::INPUT_PULLUP) {
            io_conf.mode       = GPIO_MODE_INPUT;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        } else if (Mode == GpioMode::INPUT_PULLDOWN) {
            io_conf.mode         = GPIO_MODE_INPUT;
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
        }
        io_conf.intr_type = GPIO_INTR_DISABLE;
        return gpio_config(&io_conf);
    }

    /** @brief `set` 接口。 */
    esp_err_t set(bool value) {
        if (Mode == GpioMode::OUTPUT && gpio_num_ != GPIO_NUM_NC) {
            output_value_ = value;
            gpio_set_level(gpio_num_, value);
            if (on_change_callback_ != nullptr) {
                on_change_callback_(value);
            }
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    /** @brief `get` 接口。 */
    bool get() const {
        if (Mode == GpioMode::OUTPUT) {
            return output_value_;
        }
        return gpio_get_level(gpio_num_);
    }

    /**
     * @brief : 设置引脚状态改变回调函数
     * @return  {*}
     * @param {function<void(bool)>} callback
     */
    void set_on_change_callback(std::function<void(bool)> callback) {
        on_change_callback_ = callback;
    }

  private:
    std::function<void(bool)> on_change_callback_ = nullptr;
    bool                      output_value_       = false;
};
#endif // CPP_GPIO_DRIVER_HPP
