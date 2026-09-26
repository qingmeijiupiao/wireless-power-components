/*
 * @version: no version
 * @Description: 带 NVS 持久化的输出 GPIO
 * @author: qingmeijiupiao
 */
#ifndef NVS_GPIO_OUTPUT_H
#define NVS_GPIO_OUTPUT_H

#include "driver/gpio.h"
#include "esp_err.h"
#include <functional>
#include <memory>

/**
 * @brief 带 NVS 持久化的输出 GPIO。
 *
 * 逻辑状态写入 NVS，初始化时自动恢复；持久化失败时回滚 GPIO 电平。
 * 与具体外设无关，可用于任意需要掉电保持的使能脚。
 */
class NvsGpioOutput {
  public:
    /**
     * @brief 构造控制器。
     * @param nvs_key NVS key，最长 15 字节；生命周期需覆盖对象本身。
     * @param default_state NVS 无记录时的默认逻辑状态。
     * @param active_high true：逻辑 1 输出高电平；false 反相。
     */
    explicit NvsGpioOutput(const char* nvs_key, bool default_state = false, bool active_high = true);

    /** @brief 释放内部资源。 */
    ~NvsGpioOutput();

    NvsGpioOutput(const NvsGpioOutput&)            = delete;
    NvsGpioOutput& operator=(const NvsGpioOutput&) = delete;

    /**
     * @brief 配置输出引脚，并从 NVS 恢复上次逻辑状态。
     * @param gpio 输出引脚。
     * @return ESP_OK 成功，其他值表示 GPIO 配置或恢复失败。
     */
    esp_err_t init(gpio_num_t gpio);

    /**
     * @brief 设置逻辑状态并写入 NVS。
     * @param enabled 目标逻辑状态。
     * @return ESP_OK 成功；未初始化返回 ESP_ERR_INVALID_STATE；持久化失败时回滚并返回错误。
     */
    esp_err_t set(bool enabled);

    /**
     * @brief 翻转逻辑状态并写入 NVS。
     * @return ESP_OK 成功，未初始化时返回 ESP_ERR_INVALID_STATE。
     */
    esp_err_t toggle();

    /**
     * @brief 获取当前逻辑状态。
     * @return true 表示逻辑状态为 1。
     */
    bool get() const;

    /**
     * @brief 设置状态改变回调；初始化恢复状态时也会触发一次。
     * @param callback 回调函数，参数为逻辑状态。
     */
    void set_on_change_callback(std::function<void(bool)> callback);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#endif // NVS_GPIO_OUTPUT_H
