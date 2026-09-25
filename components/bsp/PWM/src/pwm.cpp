/*
 * @Description:  PWM驱动模块实现文件
 * @Author: qingmeijiupiao
 * @version: 1.0.0
 * @Date: 2026-04-26
 */
#include "pwm.h"

static const char* TAG = "PWM";

uint8_t pwm_t::channel_used = 0;

pwm_t::~pwm_t() {
    if (initialized) {
        ledc_stop(LEDC_LOW_SPEED_MODE, channel, 0);
        channel_used &= ~(1 << (int)channel);
    }
}

esp_err_t pwm_t::init(gpio_num_t _gpio_num, uint32_t _freq_hz, ledc_timer_bit_t _duty_resolution) {
    if (initialized) {
        return ESP_OK;
    }

    int ch = -1;
    for (int i = 0; i < 4; i++) {
        if (!(channel_used & (1 << i))) {
            ch = i;
            break;
        }
    }
    if (ch < 0) {
        ESP_LOGE(TAG, "no available channel (max 4)");
        return ESP_ERR_NOT_FOUND;
    }

    channel         = (ledc_channel_t)ch;
    timer           = (ledc_timer_t)ch;
    gpio_num        = _gpio_num;
    freq_hz         = _freq_hz;
    duty_resolution = _duty_resolution;

    ledc_timer_config_t timer_conf = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = duty_resolution,
        .timer_num       = timer,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
        .deconfigure     = false,
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "timer config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t chan_conf = {
        .gpio_num    = gpio_num,
        .speed_mode  = LEDC_LOW_SPEED_MODE,
        .channel     = channel,
        .intr_type   = LEDC_INTR_DISABLE,
        .timer_sel   = timer,
        .duty        = 0,
        .hpoint      = 0,
        .sleep_mode  = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags       = {},
        .deconfigure = false,
    };
    ret = ledc_channel_config(&chan_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "channel config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    channel_used |= (1 << ch);
    initialized   = true;
    return ESP_OK;
}

float pwm_t::get_duty_percent() {
    if (!initialized) {
        return 0.0f;
    }
    uint32_t duty = ledc_get_duty(LEDC_LOW_SPEED_MODE, channel);
    return (float)duty / (1 << duty_resolution) * 100.0f;
}

esp_err_t pwm_t::set_duty_percent(float percent) {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 100.0f)
        percent = 100.0f;
    uint32_t  duty = (uint32_t)(percent / 100.0f * (1 << duty_resolution));
    esp_err_t ret  = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (ret != ESP_OK) {
        return ret;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}
