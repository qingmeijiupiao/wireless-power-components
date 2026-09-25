#include "can_resistor.h"

#include "HXC_NVS.h"
#include "cpp_gpio_driver.hpp"
#include "esp_log.h"

namespace {

constexpr const char* TAG = "CanResistor";

CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> resistor_gpio;
HXC::NVS_DATA<uint8_t>                       saved_state("can_term", 0);

} // namespace

CanResistor& CanResistor::instance() {
    static CanResistor controller;
    return controller;
}

esp_err_t CanResistor::init(gpio_num_t gpio_num) {
    if (initialized_) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    esp_err_t err = resistor_gpio.init(gpio_num);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed on pin %d: %s", gpio_num, esp_err_to_name(err));
        return err;
    }

    err = resistor_gpio.set(saved_state.read() != 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to restore saved state: %s", esp_err_to_name(err));
        return err;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "initialized on GPIO %d, state %s", gpio_num, get() ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t CanResistor::set(bool enabled) {
    if (!initialized_) {
        ESP_LOGW(TAG, "set ignored before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    const bool previous = resistor_gpio.get();
    esp_err_t  err      = resistor_gpio.set(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO write failed: %s", esp_err_to_name(err));
        return err;
    }

    err = saved_state.set(enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist state: %s", esp_err_to_name(err));
        const esp_err_t rollback_err = resistor_gpio.set(previous);
        if (rollback_err != ESP_OK) {
            ESP_LOGE(TAG, "failed to rollback GPIO state: %s", esp_err_to_name(rollback_err));
        }
    }
    return err;
}

esp_err_t CanResistor::toggle() {
    return set(!get());
}

bool CanResistor::get() const {
    return resistor_gpio.get();
}

esp_err_t CanResistor::add_on_change_callback(std::function<void(bool)> callback) {
    resistor_gpio.set_on_change_callback(callback);
    return ESP_OK;
}