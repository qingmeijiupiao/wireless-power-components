#include "HXC_TWAI.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_log.h"

struct twai_node_map_t {
    twai_node_handle_t handle;
    HXC_TWAI*          twai;
};
static twai_node_map_t twai_node_maps[MAX_TWAI_NODE_NUM];

HXC_TWAI::HXC_TWAI(uint8_t tx, uint8_t rx, uint32_t rate) : TX_PIN(tx), RX_PIN(rx), can_rate(rate) {
    // 初始化回调函数数组
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        callback_maps[i].used = false;
        callback_maps[i].addr = 0;
    }
    callback_count = 0;
}

HXC_TWAI::~HXC_TWAI() {
    if (twai_node_handle == nullptr) {
        return;
    }

    // 1. 首先停止接收任务，防止其访问即将被销毁的资源
    if (rx_task_handle != nullptr) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = nullptr;
    }

    // 2. 从静态映射表中移除当前实例记录
    for (size_t i = 0; i < sizeof(twai_node_maps) / sizeof(twai_node_maps[0]); i++) {
        if (twai_node_maps[i].handle == twai_node_handle) {
            twai_node_maps[i].handle = nullptr;
            twai_node_maps[i].twai   = nullptr;
            break;
        }
    }

    // 3. 停止并删除底层硬件节点
    twai_node_disable(twai_node_handle);
    twai_node_delete(twai_node_handle);

    // 4. 清理软件队列资源
    if (rx_queue != nullptr) {
        vQueueDelete(rx_queue);
        rx_queue = nullptr;
    }
}

void HXC_TWAI::process_receive(HXC_CAN_message_t* msg) {
    // 先调用所有地址的回调函数
    if (all_callback_func != nullptr) {
        all_callback_func(msg);
    }
    // 线性查表查找对应的回调函数
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == msg->identifier) {
            callback_maps[i].func(msg);
            break;
        }
    }
}

bool HXC_TWAI::on_rx_done_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx) {
    uint8_t      recv_buff[8];
    twai_frame_t rx_frame;
    rx_frame.buffer     = recv_buff;
    rx_frame.buffer_len = sizeof(recv_buff);

    for (size_t i = 0; i < sizeof(twai_node_maps) / sizeof(twai_node_maps[0]); i++) {
        if (twai_node_maps[i].handle == handle) {
            BaseType_t xTaskWoken = pdFALSE;

            if (ESP_OK == twai_node_receive_from_isr(handle, &rx_frame)) {

                // 组装应用层消息结构体
                HXC_CAN_message_t rx_msg;
                rx_msg.identifier       = rx_frame.header.id;
                rx_msg.data_length_code = rx_frame.header.dlc;
                rx_msg.rtr              = rx_frame.header.rtr;
                rx_msg.extd             = rx_frame.header.ide;
                memcpy(rx_msg.data, rx_frame.buffer, rx_frame.buffer_len);

                // 发送消息到队列
                if (twai_node_maps[i].twai->rx_queue != nullptr) {
                    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                    if (pdTRUE !=
                        xQueueSendFromISR(twai_node_maps[i].twai->rx_queue, &rx_msg, &xHigherPriorityTaskWoken)) {
                        // 队列已满，记录丢包计数
                        __atomic_fetch_add(&twai_node_maps[i].twai->rx_overflow_count, 1U, __ATOMIC_RELAXED);
                    }
                    xTaskWoken |= xHigherPriorityTaskWoken;
                }
            }
            // 返回 true 请求立刻进行上下文切换（如果唤醒了高优先级任务）
            return xTaskWoken == pdTRUE;
        }
    }
    return false;
}

bool HXC_TWAI::on_tx_done_callback(twai_node_handle_t, const twai_tx_done_event_data_t* edata, void* user_ctx) {
    HXC_TWAI* twai = static_cast<HXC_TWAI*>(user_ctx);
    if (twai != nullptr && !edata->is_tx_success) {
        __atomic_fetch_add(&twai->tx_failed_count, 1U, __ATOMIC_RELAXED);
    }
    return false;
}

bool HXC_TWAI::on_state_change_callback(twai_node_handle_t, const twai_state_change_event_data_t* edata,
                                        void* user_ctx) {
    HXC_TWAI* twai = static_cast<HXC_TWAI*>(user_ctx);
    if (twai != nullptr && edata->new_sta == TWAI_ERROR_BUS_OFF) {
        __atomic_fetch_add(&twai->bus_off_count, 1U, __ATOMIC_RELAXED);
    }
    return false;
}

bool HXC_TWAI::on_error_callback(twai_node_handle_t, const twai_error_event_data_t*, void* user_ctx) {
    HXC_TWAI* twai = static_cast<HXC_TWAI*>(user_ctx);
    if (twai != nullptr) {
        __atomic_fetch_add(&twai->bus_error_count, 1U, __ATOMIC_RELAXED);
    }
    return false;
}

esp_err_t HXC_TWAI::setup() {
    if (twai_node_handle != nullptr) {
        return ESP_OK;
    }

    twai_onchip_node_config_t node_config = {};
    node_config.io_cfg.tx                 = static_cast<gpio_num_t>(TX_PIN);
    node_config.io_cfg.rx                 = static_cast<gpio_num_t>(RX_PIN);
    node_config.io_cfg.quanta_clk_out     = GPIO_NUM_NC;
    node_config.io_cfg.bus_off_indicator  = GPIO_NUM_NC;
    node_config.tx_queue_depth            = TWAI_HW_TX_QUEUE_LEN; // 使用constexpr变量
    node_config.intr_priority             = TWAI_RECEIVE_INTR_PRIORITY;
    node_config.clk_src                   = TWAI_CLK_SRC_DEFAULT;
    node_config.fail_retry_cnt            = TWAI_HW_TX_RETRY_CNT;
    node_config.bit_timing.bitrate        = can_rate;

    esp_err_t ret = twai_new_node_onchip(&node_config, &twai_node_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("TWAI", "node creation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 等待底层资源完全就绪
    while (twai_node_handle == nullptr) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    // 注册到全局静态映射表
    for (size_t i = 0; i < sizeof(twai_node_maps) / sizeof(twai_node_maps[0]); i++) {
        if (twai_node_maps[i].handle == nullptr) {
            twai_node_maps[i].handle = twai_node_handle;
            twai_node_maps[i].twai   = this;
            break;
        }
    }

    // 注册中断回调
    twai_event_callbacks_t user_cbs = {};
    user_cbs.on_tx_done             = on_tx_done_callback;
    user_cbs.on_rx_done             = on_rx_done_callback;
    user_cbs.on_state_change        = on_state_change_callback;
    user_cbs.on_error               = on_error_callback;
    ret                             = twai_node_register_event_callbacks(twai_node_handle, &user_cbs, this);
    if (ret != ESP_OK) {
        ESP_LOGE("TWAI", "callback registration failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 硬件过滤器配置
    if (user_set_filter) {
        ret = twai_node_config_mask_filter(twai_node_handle, 0, &filter_config);
        if (ret != ESP_OK) {
            ESP_LOGE("TWAI", "Failed to configure hardware filter");
            return ret;
        }
    }

    // 启动TWAI节点
    ret = twai_node_enable(twai_node_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("TWAI", "node enable failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 创建软件接收队列
    rx_queue = xQueueCreate(TWAI_RX_QUEUE_LEN, sizeof(HXC_CAN_message_t));
    if (rx_queue == nullptr) {
        ESP_LOGE("TWAI", "RX queue creation failed");
        return ESP_ERR_NO_MEM;
    }

    // 创建异步处理任务
    if (xTaskCreate(receive_task, "twai_rx_task", TWAI_RECEIVE_TASK_STACK, this, TWAI_RECEIVE_TASK_PRIO,
                    &rx_task_handle) != pdPASS) {
        ESP_LOGE("TWAI", "RX task creation failed");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t HXC_TWAI::set_filter(twai_mask_filter_config_t filter) {
    filter_config   = filter;
    user_set_filter = true;

    if (get_setup_flag()) {
        // 修改过滤器需要进入Reset模式（Disable节点）
        esp_err_t ret = twai_node_disable(twai_node_handle);
        if (ret != ESP_OK)
            return ret;

        ret = twai_node_config_mask_filter(twai_node_handle, 0, &filter_config);
        if (ret != ESP_OK)
            return ret;

        ret = twai_node_enable(twai_node_handle);
        if (ret != ESP_OK)
            return ret;
    }
    return ESP_OK;
}

void HXC_TWAI::add_can_receive_callback_func(int addr, HXC_can_feedback_func func) {
    if (addr == -1) {
        all_callback_func = func;
        return;
    }

    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            callback_maps[i].func = func;
            return;
        }
    }

    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (!callback_maps[i].used) {
            callback_maps[i].addr = addr;
            callback_maps[i].func = func;
            callback_maps[i].used = true;
            callback_count++;
            return;
        }
    }
}

void HXC_TWAI::remove_can_receive_callback_func(int addr) {
    if (addr == -1) {
        all_callback_func = nullptr;
        return;
    }
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            callback_maps[i].used = false;
            callback_maps[i].addr = 0;
            callback_count--;
            return;
        }
    }
}

bool HXC_TWAI::exist_can_receive_callback_func(int addr) {
    if (addr == -1)
        return all_callback_func != nullptr;
    for (int i = 0; i < MAX_TWAI_CALLBACK_NUM; i++) {
        if (callback_maps[i].used && callback_maps[i].addr == addr) {
            return true;
        }
    }
    return false;
}

bool HXC_TWAI::get_setup_flag() {
    return twai_node_handle != nullptr;
}

esp_err_t HXC_TWAI::get_info(twai_node_status_t* status, twai_node_record_t* statistics) const {
    if (twai_node_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return twai_node_get_info(twai_node_handle, status, statistics);
}

// 【线程安全发送】：使用局部栈变量，支持多任务并发调用
esp_err_t HXC_TWAI::send(HXC_CAN_message_t* message) {
    if (twai_node_handle == nullptr || message == nullptr) {
        return ESP_FAIL;
    }

    twai_frame_t tx_frame          = {};
    uint8_t      local_data_buf[8] = {0};

    tx_frame.header.ide = message->extd;
    tx_frame.header.rtr = message->rtr;
    tx_frame.header.id  = message->identifier;
    tx_frame.header.dlc = message->data_length_code;

    for (size_t i = 0; i < message->data_length_code; i++) {
        local_data_buf[i] = message->data[i];
    }

    tx_frame.buffer     = local_data_buf;
    tx_frame.buffer_len = message->data_length_code;

    // 底层会拷贝数据到硬件FIFO或内部队列，因此局部变量销毁是安全的
    return twai_node_transmit(twai_node_handle, &tx_frame, TWAI_SEND_TIMEOUT_MS);
}

void HXC_TWAI::receive_task(void* arg) {
    HXC_TWAI*         twai = reinterpret_cast<HXC_TWAI*>(arg);
    HXC_CAN_message_t received_msg;

    while (true) {
        // 阻塞等待接收队列数据，CPU占用率为0%
        if (xQueueReceive(twai->rx_queue, &received_msg, portMAX_DELAY) == pdTRUE) {
            twai->process_receive(&received_msg);
        }
    }
}
