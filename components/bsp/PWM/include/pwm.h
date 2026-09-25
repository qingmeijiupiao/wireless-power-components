/*
 * @Description: PWM驱动模块
 * @Author: qingmeijiupiao
 * @version: 1.0.0
 * @Date: 2026-04-26
 */
#ifndef PWM_H
#define PWM_H
#include "esp_err.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
class pwm_t {
  public:
    /** @brief `pwm_t` 接口。 */
    pwm_t() = default;

    /**
     * @description: 析构函数,自动停止PWM输出并释放通道
     */
    ~pwm_t();

    /**
     * @description: 初始化PWM,自动分配通道和定时器(最多4路)
     * @param {gpio_num_t} _gpio_num PWM输出GPIO引脚
     * @param {uint32_t} _freq_hz PWM频率,默认5000Hz
     * @param {ledc_timer_bit_t} _duty_resolution 占空比分辨率,默认13位
     * @return {esp_err_t} 成功返回ESP_OK,通道已满返回ESP_ERR_NOT_FOUND
     */
    esp_err_t init(gpio_num_t _gpio_num, uint32_t _freq_hz = 5000,
                   ledc_timer_bit_t _duty_resolution = LEDC_TIMER_13_BIT);

    /**
     * @description: 获取当前占空比百分比
     * @return {float} 占空比百分比,0.0~100.0
     */
    float get_duty_percent();

    /**
     * @description: 设置占空比百分比并立即生效
     * @param {float} percent 占空比百分比,0.0~100.0,超出范围会自动限幅
     * @return {esp_err_t} 成功返回ESP_OK,未初始化返回ESP_ERR_INVALID_STATE
     */
    esp_err_t set_duty_percent(float percent);

  private:
    ledc_channel_t   channel;
    ledc_timer_t     timer;
    gpio_num_t       gpio_num;
    uint32_t         freq_hz;
    ledc_timer_bit_t duty_resolution;
    bool             initialized = false;
    static uint8_t   channel_used;
};

#endif
