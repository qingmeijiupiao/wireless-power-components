/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: ADC驱动类，用于读取ADC通道的电压值
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:36:16
 */

#ifndef ADC_H
#define ADC_H
#include "esp_err.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"

class adc_t {
  public:
    /** @brief `adc_t` 接口。 */
    adc_t(adc_channel_t _channel) : adc_channel(_channel) {};
    /** @brief `~adc_t` 接口。 */
    ~adc_t() {};
    /**
     * @brief 初始化 ADC 单次采样通道。
     * @return ESP_OK 初始化成功；其他值表示驱动配置失败。
     */
    esp_err_t init();

    /**
     * @brief 读取 ADC 原始采样值。
     * @param raw 接收原始采样值。
     * @return ESP_OK 读取成功；其他值表示采样失败。
     */
    esp_err_t read_raw(int& raw);

    /**
     * @brief 读取校准后的 ADC 电压。
     * @param voltage 接收电压值，单位 mV。
     * @return ESP_OK 读取成功；其他值表示采样或校准失败。
     */
    esp_err_t read_voltage_mV(int& voltage);

  private:
    adc_channel_t                    adc_channel;
    adc_cali_handle_t                cali_handle;
    static adc_oneshot_unit_handle_t adc1_unit_handle;
};

#endif
