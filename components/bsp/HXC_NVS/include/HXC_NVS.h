/*
 * @LastEditors: qingmeijiupiao
 * @Description: HXC战队nvs储存系统二次封装库，纯 ESP-IDF 框架，支持 char*
 * @Author: qingmeijiupiao
 */
#ifndef HXC_NVS_H
#define HXC_NVS_H

#include <atomic>
#include <cstring>
#include <type_traits>
#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace HXC {

// 默认使用的 NVS 命名空间名称
#define NVS_NAME "HXC"

// 基类，用于共享静态变量及 NVS 句柄
class NVS_Base {
  public:
    /** @brief `setup` 接口。 */
    static esp_err_t setup();

  protected:
    static std::atomic_bool is_setup;
    static nvs_handle_t     _handle;
};

// ---------------------------------------------------------
// 泛型模板类声明与实现 (基础数据类型)
// ---------------------------------------------------------
template <typename Value_type> class NVS_DATA : public NVS_Base {
  public:
    // 构造函数，初始化 key 和默认值
    /** @brief `NVS_DATA` 接口。 */
    NVS_DATA(const char* _key, Value_type default_value) {
        static_assert(!std::is_pointer<Value_type>::value, "NVS_DATA<Value_type>: Cannot use pointer type!");
        static_assert(std::is_trivially_copyable<Value_type>::value,
                      "NVS_DATA<Value_type>: Value_type must be trivially copyable");

        // NVS key 最大长度为 15 字节
        strncpy(this->key, _key, 15);
        this->key[15] = '\0';
        if (strlen(_key) > 15) {
            ESP_LOGE("HXC_NVS", "NVS key too long, truncated: %s", this->key);
        }
        this->value = default_value;
    }

    // 保存当前缓存值到 NVS。
    /** @brief `save` 接口。 */
    esp_err_t save() {
        esp_err_t err = setup();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_set_blob(_handle, key, static_cast<const void*>(&value), sizeof(Value_type));
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "nvs_set_blob fail: %s %s", key, esp_err_to_name(err));
            return err;
        }
        err = nvs_commit(_handle);
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "nvs_commit fail: %s %s", key, esp_err_to_name(err));
            return err;
        }
        is_read = true;
        return ESP_OK;
    }

    // 持久化成功后才更新缓存，调用方可可靠判断写入结果。
    /** @brief `set` 接口。 */
    esp_err_t set(const Value_type& new_value) {
        esp_err_t err = setup();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_set_blob(_handle, key, static_cast<const void*>(&new_value), sizeof(Value_type));
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "nvs_set_blob fail: %s %s", key, esp_err_to_name(err));
            return err;
        }
        err = nvs_commit(_handle);
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "nvs_commit fail: %s %s", key, esp_err_to_name(err));
            return err;
        }
        value   = new_value;
        is_read = true;
        return ESP_OK;
    }

    // 从 NVS 读取数据
    /** @brief `read` 接口。 */
    Value_type read() {
        if (setup() != ESP_OK) {
            return value;
        }
        if (is_read)
            return value;

        size_t    datalen = 0;
        esp_err_t err     = nvs_get_blob(_handle, key, NULL, &datalen);
        if (err != ESP_OK || datalen != sizeof(Value_type)) {
            ESP_LOGW("HXC_NVS", "KEY=%s NVS no data or length mismatch, use default value", key);
            is_read = true;
            return value;
        }

        err = nvs_get_blob(_handle, key, (void*)&value, &datalen);
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "KEY=%s NVS read failed, using default value", key);
            return value;
        }

        is_read = true;
        return value;
    }

    // 重载赋值运算符
    NVS_DATA& operator=(const Value_type& newValue) {
        const esp_err_t err = set(newValue);
        if (err != ESP_OK) {
            ESP_LOGE("HXC_NVS", "assignment persist failed: %s %s", key, esp_err_to_name(err));
        }
        return *this;
    }

    // 重载隐式类型转换
    /** @brief `Value_type` 接口。 */
    operator Value_type() {
        return read();
    }

  protected:
    char       key[16];
    Value_type value;
    bool       is_read = false;
};

// ---------------------------------------------------------
// 针对 char* 类型的特化模板类声明 (实现转移至 .cpp)
// ---------------------------------------------------------
template <> class NVS_DATA<char*> : public NVS_Base {
  public:
    /** @brief `NVS_DATA` 接口。 */
    NVS_DATA(const char* _key, const char* default_value);
    /** @brief `~NVS_DATA` 接口。 */
    ~NVS_DATA();
    /** @brief `save` 接口。 */
    esp_err_t save();
    /** @brief `set` 接口。 */
    esp_err_t set(const char* new_value);
    /** @brief `read` 接口。 */
    char*     read();
    NVS_DATA& operator=(const char* newValue);
    /** @brief 返回当前缓存字符串；需要时会先从 NVS 延迟加载。 */
    operator char*();

  protected:
    char  key[16];
    char* value;
    bool  is_read;
};

} // namespace HXC

#endif // HXC_NVS_H
