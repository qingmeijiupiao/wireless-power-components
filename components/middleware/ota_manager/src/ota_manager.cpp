#include "ota_manager.h"

#include "esp_ota_ops.h"

namespace OtaManager {
namespace {

State                  state            = State::IDLE;
esp_ota_handle_t       ota_handle       = 0;
size_t                 image_size       = 0;
size_t                 bytes_written    = 0;
const esp_partition_t* target_partition = nullptr;

void reset_session() {
    state            = State::IDLE;
    ota_handle       = 0;
    image_size       = 0;
    bytes_written    = 0;
    target_partition = nullptr;
}

esp_err_t get_app_description(const esp_partition_t* partition, esp_app_desc_t* app_desc) {
    if (partition == nullptr || app_desc == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_ota_get_partition_description(partition, app_desc);
}

} // namespace

esp_err_t begin(size_t requested_image_size) {
    if (state != State::IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (requested_image_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t* next_partition = esp_ota_get_next_update_partition(nullptr);
    if (next_partition == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    if (requested_image_size != IMAGE_SIZE_UNKNOWN && requested_image_size > next_partition->size) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t new_handle = 0;
    esp_err_t        err        = esp_ota_begin(next_partition, requested_image_size, &new_handle);
    if (err != ESP_OK) {
        return err;
    }

    state            = State::WRITING;
    ota_handle       = new_handle;
    image_size       = requested_image_size;
    bytes_written    = 0;
    target_partition = next_partition;
    return ESP_OK;
}

esp_err_t write(const void* data, size_t size) {
    if (state != State::WRITING) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr && size != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (image_size != IMAGE_SIZE_UNKNOWN && size > image_size - bytes_written) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (size > target_partition->size - bytes_written) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_write(ota_handle, data, size);
    if (err == ESP_OK) {
        bytes_written += size;
    }
    return err;
}

esp_err_t finish() {
    if (state != State::WRITING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        reset_session();
        return err;
    }

    state      = State::VERIFIED;
    ota_handle = 0;
    return ESP_OK;
}

esp_err_t activate() {
    if (state != State::VERIFIED) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_set_boot_partition(target_partition);
    if (err != ESP_OK) {
        return err;
    }

    state = State::READY_TO_REBOOT;
    return ESP_OK;
}

esp_err_t abort() {
    if (state == State::VERIFIED) {
        reset_session();
        return ESP_OK;
    }
    if (state != State::WRITING) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ota_abort(ota_handle);
    reset_session();
    return err;
}

Status get_status() {
    return {
        .state            = state,
        .image_size       = image_size,
        .bytes_written    = bytes_written,
        .target_partition = target_partition,
    };
}

const esp_partition_t* get_running_partition() {
    return esp_ota_get_running_partition();
}

const esp_partition_t* get_boot_partition() {
    return esp_ota_get_boot_partition();
}

const esp_partition_t* get_target_partition() {
    return target_partition;
}

const esp_partition_t* get_next_update_partition() {
    return esp_ota_get_next_update_partition(nullptr);
}

esp_err_t get_running_app_description(esp_app_desc_t* app_desc) {
    return get_app_description(get_running_partition(), app_desc);
}

esp_err_t get_boot_app_description(esp_app_desc_t* app_desc) {
    return get_app_description(get_boot_partition(), app_desc);
}

esp_err_t get_target_app_description(esp_app_desc_t* app_desc) {
    return get_app_description(get_target_partition(), app_desc);
}

esp_err_t confirm_running_firmware() {
    return esp_ota_mark_app_valid_cancel_rollback();
}

bool rollback_is_possible() {
    return esp_ota_check_rollback_is_possible();
}

esp_err_t rollback_and_reboot() {
    return esp_ota_mark_app_invalid_rollback_and_reboot();
}

} // namespace OtaManager
