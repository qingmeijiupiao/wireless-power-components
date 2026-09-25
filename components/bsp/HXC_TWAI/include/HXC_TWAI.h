/*
 * @version: 2.2
 * @LastEditors: Please set LastEditors
 * @Description: HXC ESP32 twai封装类
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-04-29 04:12:41
 */
#ifndef HXC_TWAI_H
#define HXC_TWAI_H

#include <cstring>
#include <functional>
#include "esp_twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// 性能与资源调节参数 (根据应用场景调整)

/** 软件分发映射表大小：决定了你能同时为多少个特定 ID 注册独立回调函数 */
constexpr uint8_t MAX_TWAI_CALLBACK_NUM = 10;

/** 接收任务优先级：若处理 8000fps 等极端场景，建议设为 10 以上以抢占普通应用 */
constexpr UBaseType_t TWAI_RECEIVE_TASK_PRIO = 2;

/** 接收任务栈大小：4096 字节足以应对大多数包含打印逻辑的回调 */
constexpr uint32_t TWAI_RECEIVE_TASK_STACK = 4096;

/** 接收中断优先级[0-3]：默认设为 0*/
constexpr uint32_t TWAI_RECEIVE_INTR_PRIORITY = 0;

/** 软件接收队列深度：在 8000fps 负载下，深度 50-100 可提供更稳健的系统抖动缓冲 */
constexpr uint32_t TWAI_RX_QUEUE_LEN = 20;

/** 发送阻塞超时时间 (ms)：当发送硬件 FIFO 满时，函数允许等待的时间 */
constexpr TickType_t TWAI_SEND_TIMEOUT_MS = pdMS_TO_TICKS(100);

/** 硬件发送队列深度：ESP-IDF 驱动内部的暂存队列深度 */
constexpr uint32_t TWAI_HW_TX_QUEUE_LEN = 5;

/** 硬件发送重试次数：在发送失败时，尝试重试的次数 */
constexpr uint32_t TWAI_HW_TX_RETRY_CNT = 5;

/** 最大TWAI节点数量：决定了你能同时管理多少个TWAI节点 */
constexpr uint8_t MAX_TWAI_NODE_NUM = 3;

// ==========================================

// CAN消息结构体
struct HXC_CAN_message_t {
    uint8_t  extd             = 0;   /**< 扩展帧格式标志（29位ID） */
    uint8_t  rtr              = 0;   /**< 远程帧标志 */
    uint8_t  data_length_code = 0;   /**< 数据长度代码 */
    uint8_t  reserved         = 0;   /**< 保留字节，对齐用 */
    uint32_t identifier       = 0;   /**< 11或29位标识符 */
    uint8_t  data[8]          = {0}; /**< 数据字节（在RTR帧中无关） */
} __attribute__((packed));

// CAN消息接收回调函数
using HXC_can_feedback_func = std::function<void(HXC_CAN_message_t* can_message)>;

// 回调函数映射结构体
struct callback_map_t {
    int                   addr;
    HXC_can_feedback_func func;
    bool                  used;
};

constexpr uint32_t operator""_Mbps(unsigned long long x) {
    return static_cast<uint32_t>(x * 1'000'000);
}

constexpr uint32_t operator""_Kbps(unsigned long long x) {
    return static_cast<uint32_t>(x * 1'000);
}

// TWAI封装类
class HXC_TWAI {
  public:
    // 防止值传递TWAI对象
    /** @brief `HXC_TWAI` 接口。 */
    HXC_TWAI(const HXC_TWAI&)            = delete; /**< 删除拷贝构造函数 */
    HXC_TWAI& operator=(const HXC_TWAI&) = delete; /**< 删除拷贝赋值函数 */
    HXC_TWAI(HXC_TWAI&&)                 = delete; /**< 删除移动构造函数 */
    HXC_TWAI& operator=(HXC_TWAI&&)      = delete; /**< 删除移动赋值函数 */

    /**
     * @description: TWAI构造函数, 默认为HXC开发板A的引脚
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {uint8_t} tx 连接can收发芯片TX引脚的IO号
     * @param {uint8_t} rx 连接can收发芯片RX引脚的IO号
     * @param {CAN_RATE} rate CAN速率
     */
    HXC_TWAI(uint8_t tx = 8, uint8_t rx = 18, uint32_t rate = 1000000);

    /**
     * @description: 析构函数
     */
    ~HXC_TWAI();

    /**
     * @description: TWAI初始化
     * @return {esp_err_t} 成功返回ESP_OK
     * @Author: qingmeijiupiao
     */
    esp_err_t setup();

    /**
     * @description: 获取初始化状态
     * @return {bool} true:已初始化 false:未初始化
     * @Author: qingmeijiupiao
     */
    bool get_setup_flag();

    /**
     * @description: 设置CAN过滤器
     * @param {twai_mask_filter_config_t} filter 过滤器配置
     * @note 配置了过滤器时，接收标准CAN消息和扩展帧格式的消息只能二选一
     * @note 最好在setup之前设置过滤器，否则会暂停一段时间twai节点
     * @return {esp_err_t} 成功返回ESP_OK
     * @Author: qingmeijiupiao
     */
    esp_err_t set_filter(twai_mask_filter_config_t filter);

    /**
     * @description: 发送CAN消息
     * @return {esp_err_t}
     * @Author: qingmeijiupiao
     * @param {HXC_CAN_message_t*} message CAN消息
     */
    esp_err_t send(HXC_CAN_message_t* message);

    /**
     * @description: 添加CAN消息接收回调,收到对应地址的消息时运行回调函数
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址 -1表示所有地址
     * @param {HXC_can_feedback_func} func 回调函数
     */
    void add_can_receive_callback_func(int addr, HXC_can_feedback_func func);

    /**
     * @description: 移除CAN消息接收回调函数
     * @return {*}
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址 -1表示所有地址
     */
    void remove_can_receive_callback_func(int addr);

    /**
     * @description: 判断CAN消息接收回调函数是否存在
     * @return {bool} true:存在 false:不存在
     * @Author: qingmeijiupiao
     * @param {int} addr CAN消息地址 -1表示所有地址回调函数是否存在
     */
    bool exist_can_receive_callback_func(int addr);

    /**
     * @description: 获取接收队列溢出计数
     * @return {uint32_t} 溢出次数
     */
    uint32_t get_rx_overflow_count() const {
        return __atomic_load_n(&rx_overflow_count, __ATOMIC_RELAXED);
    }
    /** @brief `get_tx_failed_count` 接口。 */
    uint32_t get_tx_failed_count() const {
        return __atomic_load_n(&tx_failed_count, __ATOMIC_RELAXED);
    }
    /** @brief `get_bus_off_count` 接口。 */
    uint32_t get_bus_off_count() const {
        return __atomic_load_n(&bus_off_count, __ATOMIC_RELAXED);
    }
    /** @brief `get_bus_error_count` 接口。 */
    uint32_t get_bus_error_count() const {
        return __atomic_load_n(&bus_error_count, __ATOMIC_RELAXED);
    }
    /** @brief `get_info` 接口。 */
    esp_err_t get_info(twai_node_status_t* status, twai_node_record_t* statistics) const;

  protected:
    twai_node_handle_t        twai_node_handle = nullptr;           /**< TWAI节点句柄 */
    uint8_t                   TX_PIN, RX_PIN;                       /**< 连接CAN收发芯片的TX和RX引脚IO号 */
    twai_mask_filter_config_t filter_config;                        /**< 过滤器配置 */
    bool                      user_set_filter = false;              /**< 用户是否设置了过滤器 */
    uint32_t                  can_rate        = 1000000;            /**< CAN速率 */
    callback_map_t            callback_maps[MAX_TWAI_CALLBACK_NUM]; /**< 回调函数线性映射表 */
    uint8_t                   callback_count = 0;                   /**< 当前回调函数数量 */

    QueueHandle_t         rx_queue       = nullptr; /**< 接收队列句柄，用于解决高并发数据覆盖问题 */
    TaskHandle_t          rx_task_handle = nullptr; /**< 接收任务句柄 */
    uint32_t              rx_overflow_count = 0;    /**< 接收队列溢出计数 */
    uint32_t              tx_failed_count   = 0;
    uint32_t              bus_off_count     = 0;
    uint32_t              bus_error_count   = 0;
    HXC_can_feedback_func all_callback_func = nullptr; /**< 所有地址的回调函数 */

    /**
     * @description: TWAI接受数据的硬件中断回调，接收到数据后推入队列
     */
    static bool on_rx_done_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t* edata, void* user_ctx);
    /** @brief `on_tx_done_callback` 接口。 */
    static bool on_tx_done_callback(twai_node_handle_t handle, const twai_tx_done_event_data_t* edata, void* user_ctx);
    static bool on_state_change_callback(twai_node_handle_t handle, const twai_state_change_event_data_t* edata,
                                         void* user_ctx);
    /** @brief `on_error_callback` 接口。 */
    static bool on_error_callback(twai_node_handle_t handle, const twai_error_event_data_t* edata, void* user_ctx);

    /**
     * @description: TWAI处理数据的独立任务，从队列阻塞读取
     */
    static void receive_task(void* arg);

    /**
     * @description: 将队列取出的消息转换并分发给对应的应用层回调函数
     */
    void process_receive(HXC_CAN_message_t* msg);
};

#endif
