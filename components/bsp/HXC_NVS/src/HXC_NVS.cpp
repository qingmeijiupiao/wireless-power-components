#include "HXC_NVS.h"

#include <new>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace HXC {

static const char* TAG = "HXC_NVS";

// 初始化 NVS_Base 静态成员
std::atomic_bool NVS_Base::is_setup = false;
nvs_handle_t     NVS_Base::_handle  = 0;

esp_err_t NVS_Base::setup() {
    if (is_setup.load(std::memory_order_acquire)) {
        return ESP_OK;
    }

    static SemaphoreHandle_t setup_mutex = xSemaphoreCreateMutex();
    if (setup_mutex == nullptr) {
        ESP_LOGE(TAG, "failed to create setup mutex");
        return ESP_ERR_NO_MEM;
    }
    if (xSemaphoreTake(setup_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (is_setup.load(std::memory_order_acquire)) {
        xSemaphoreGive(setup_mutex);
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        xSemaphoreGive(setup_mutex);
        return err;
    }

    err = nvs_open(NVS_NAME, NVS_READWRITE, &_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        xSemaphoreGive(setup_mutex);
        return err;
    }
    ESP_LOGI(TAG, "nvs_open %s success", NVS_NAME);
    is_setup.store(true, std::memory_order_release);
    xSemaphoreGive(setup_mutex);
    return ESP_OK;
}

// =========================================================
// NVS_DATA<char*> 特化类的具体实现
// =========================================================

NVS_DATA<char*>::NVS_DATA(const char* _key, const char* default_value) : value(nullptr), is_read(false) {
    strncpy(this->key, _key, 15);
    this->key[15] = '\0';
    if (strlen(_key) > 15) {
        ESP_LOGE(TAG, "nvs key too long, truncated: %s", this->key);
    }

    // 深拷贝默认值
    if (default_value) {
        const size_t length = strlen(default_value) + 1;
        this->value         = new (std::nothrow) char[length];
        if (this->value != nullptr) {
            memcpy(this->value, default_value, length);
        }
    }
}

NVS_DATA<char*>::~NVS_DATA() {
    if (value) {
        delete[] value;
        value = nullptr;
    }
}

esp_err_t NVS_DATA<char*>::save() {
    esp_err_t err = setup();
    if (err != ESP_OK) {
        return err;
    }
    if (!value)
        return ESP_FAIL;

    err = nvs_set_str(_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_str fail: %s %s", key, esp_err_to_name(err));
        return err;
    }
    err = nvs_commit(_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit fail: %s %s", key, esp_err_to_name(err));
        return err;
    }
    is_read = true;
    return ESP_OK;
}

esp_err_t NVS_DATA<char*>::set(const char* new_value) {
    if (new_value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t length      = strlen(new_value) + 1;
    char*        replacement = new (std::nothrow) char[length];
    if (replacement == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(replacement, new_value, length);

    esp_err_t err = setup();
    if (err == ESP_OK) {
        err = nvs_set_str(_handle, key, replacement);
    }
    if (err == ESP_OK) {
        err = nvs_commit(_handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "persist string failed: %s %s", key, esp_err_to_name(err));
        delete[] replacement;
        return err;
    }

    delete[] value;
    value   = replacement;
    is_read = true;
    return ESP_OK;
}

char* NVS_DATA<char*>::read() {
    if (setup() != ESP_OK) {
        return value;
    }
    if (is_read)
        return value;

    size_t    datalen = 0;
    // 获取所需长度（包含 \0）
    esp_err_t err     = nvs_get_str(_handle, key, NULL, &datalen);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "KEY=%s NVS无数据, 使用默认值", key);
        is_read = true;
        return value;
    }

    char* arr = new (std::nothrow) char[datalen];
    if (arr == nullptr) {
        ESP_LOGE(TAG, "KEY=%s allocation failed", key);
        return value;
    }
    err = nvs_get_str(_handle, key, arr, &datalen);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "KEY=%s NVS读取失败, 使用默认值", key);
        delete[] arr;
        return value;
    }

    // 释放旧值内存并指向从 NVS 读出的新值
    if (value) {
        delete[] value;
    }
    value   = arr;
    is_read = true;
    return value;
}

NVS_DATA<char*>& NVS_DATA<char*>::operator=(const char* newValue) {
    const esp_err_t err = set(newValue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "assignment persist failed: %s %s", key, esp_err_to_name(err));
    }
    return *this;
}

NVS_DATA<char*>::operator char*() {
    return read();
}

} // namespace HXC
