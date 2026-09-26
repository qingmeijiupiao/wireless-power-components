#include "nvs_gpio_output.h"

#include "HXC_NVS.h"
#include "cpp_gpio_driver.hpp"
#include "esp_log.h"

#include <utility>

namespace {

constexpr const char* TAG = "NvsGpioOutput";

} // namespace

struct NvsGpioOutput::Impl {
    Impl(const char* key, bool def, bool active)
        : nvs_key(key),
          default_state(def),
          active_high(active),
          saved(key, static_cast<uint8_t>(def ? 1 : 0)) {}

    bool to_raw(bool logical) const {
        return active_high ? logical : !logical;
    }

    bool to_logical(bool raw) const {
        return active_high ? raw : !raw;
    }

    const char*                                  nvs_key;
    bool                                         default_state;
    bool                                         active_high;
    gpio_num_t                                   gpio = GPIO_NUM_NC;
    CppGpioDriver<GPIO_NUM_NC, GpioMode::OUTPUT> gpio_driver;
    HXC::NVS_DATA<uint8_t>                       saved;
    bool                                         initialized = false;
    std::function<void(bool)>                    on_change;
};

NvsGpioOutput::NvsGpioOutput(const char* nvs_key, bool default_state, bool active_high)
    : impl_(std::make_unique<Impl>(nvs_key, default_state, active_high)) {}

NvsGpioOutput::~NvsGpioOutput() = default;

esp_err_t NvsGpioOutput::init(gpio_num_t gpio) {
    if (impl_->initialized) {
        return ESP_OK;
    }
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "invalid GPIO for key %s", impl_->nvs_key);
        return ESP_ERR_INVALID_ARG;
    }
    impl_->gpio = gpio;

    esp_err_t err = impl_->gpio_driver.init(gpio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO init failed on pin %d: %s", gpio, esp_err_to_name(err));
        return err;
    }

    const bool logical = impl_->saved.read() != 0;
    err                = impl_->gpio_driver.set(impl_->to_raw(logical));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to restore saved state: %s", esp_err_to_name(err));
        return err;
    }

    impl_->initialized = true;
    ESP_LOGI(TAG, "key=%s restored on GPIO %d, state %s", impl_->nvs_key, gpio, logical ? "ON" : "OFF");

    if (impl_->on_change != nullptr) {
        impl_->on_change(logical);
    }
    return ESP_OK;
}

esp_err_t NvsGpioOutput::set(bool enabled) {
    if (!impl_->initialized) {
        ESP_LOGW(TAG, "set ignored before initialization");
        return ESP_ERR_INVALID_STATE;
    }

    const bool previous = get();
    esp_err_t  err      = impl_->gpio_driver.set(impl_->to_raw(enabled));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO write failed: %s", esp_err_to_name(err));
        return err;
    }

    err = impl_->saved.set(static_cast<uint8_t>(enabled ? 1 : 0));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to persist state: %s", esp_err_to_name(err));
        const esp_err_t rollback_err = impl_->gpio_driver.set(impl_->to_raw(previous));
        if (rollback_err != ESP_OK) {
            ESP_LOGE(TAG, "failed to rollback GPIO state: %s", esp_err_to_name(rollback_err));
        }
        return err;
    }

    if (impl_->on_change != nullptr) {
        impl_->on_change(enabled);
    }
    return ESP_OK;
}

esp_err_t NvsGpioOutput::toggle() {
    return set(!get());
}

bool NvsGpioOutput::get() const {
    if (!impl_->initialized) {
        return false;
    }
    return impl_->to_logical(impl_->gpio_driver.get());
}

void NvsGpioOutput::set_on_change_callback(std::function<void(bool)> callback) {
    impl_->on_change = std::move(callback);
}
