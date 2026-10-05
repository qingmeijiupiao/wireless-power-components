#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace EspNowLink {

constexpr size_t MAC_ADDRESS_SIZE = 6;
constexpr size_t KEY_SIZE         = 16;
constexpr size_t MAX_PAYLOAD_SIZE = 224;

struct MacAddress {
    uint8_t bytes[MAC_ADDRESS_SIZE];

    bool operator==(const MacAddress& other) const;
    bool operator!=(const MacAddress& other) const;
    /** @return true 表示该地址为 ESP-NOW 广播地址。 */
    bool is_broadcast() const;
};

/** ESP-NOW 广播 MAC 地址 FF:FF:FF:FF:FF:FF。 */
extern const MacAddress BROADCAST_ADDRESS;

struct SavedPeer {
    MacAddress address;
    uint8_t    last_channel;
    uint8_t    role = 0; /**< 应用定义的角色编号；0 为旧协议未知角色。 */
};

/** 通用配对策略；角色编号 1..31 及接受掩码由产品定义，链路不解释其业务含义。 */
struct PairingConfig {
    uint8_t local_role = 0;
    uint32_t accepted_roles = 0; /**< 0 接受任意角色；否则 bit(role) 为允许项。 */
    uint8_t peer_limit = 3;      /**< 1..3，不改变 V1 NVS 布局。 */
    bool replace_existing = false; /**< 仅 peer_limit=1 可用，成功时原子替换旧绑定。 */
    bool allow_legacy = true;   /**< 允许角色未知、仅链路确认的旧协议对端。 */
    uint32_t scan_timeout_ms = 15000; /**< 主动配对总时限，1000..60000 ms。 */
};

/** 单次配对终态快照；serial 每次终结递增，可用于产品层去重记录。 */
struct PairingResult {
    uint32_t serial = 0;
    MacAddress peer = {};
    uint8_t channel = 0;
    esp_err_t error = ESP_ERR_INVALID_STATE;
    bool initiator = false;
    bool legacy = false; /**< 旧对端不提供持久化结果确认。 */
};

/** 业务包交付语义。 */
enum class Delivery : uint8_t {
    BEST_EFFORT = 0,
    RELIABLE,
};

/** 可靠发送 ACK 超时来源。 */
enum class TimeoutMode : uint8_t {
    FIXED = 0,
    ADAPTIVE,
};

struct SendOptions {
    Delivery    delivery        = Delivery::RELIABLE;
    TimeoutMode timeout_mode    = TimeoutMode::ADAPTIVE;
    uint16_t    ack_timeout_ms  = 0;     /**< 0 表示使用 peer 或全局默认值。 */
    uint8_t     max_attempts    = 5;     /**< 包含首次发送。 */
    bool        allow_plaintext = false; /**< 仅供配对阶段显式启用。 */
};

/** APP 接收消息视图，仅在 MessageHandler 返回前有效。 */
struct Message {
    MacAddress     source;
    MacAddress     destination;
    uint16_t       message_id;
    uint32_t       sequence;
    const uint8_t* payload;
    size_t         payload_size;
    int8_t         rssi;
    uint8_t        channel;
    bool           reliable;
};

enum class SendResult : uint8_t {
    ACKNOWLEDGED = 0,
    SENT,
    NO_ACK,
    SUBMIT_FAILED,
    MAC_FAILED,
    CANCELLED, /**< 被新控制代际取消，已提交驱动的帧仍可能到达对端。 */
};

using MessageHandler = void (*)(const Message& message, void* context);
using SendCallback   = void (*)(SendResult result, uint32_t sequence, void* context);

/** ESP-NOW 单播 peer 运行期配置。 */
struct PeerConfig {
    MacAddress address;
    uint8_t    lmk[KEY_SIZE];
    uint8_t    channel;
    bool       encrypted;
};

/** 本次运行期间的链路诊断计数，不持久化到 NVS。 */
struct LinkStatistics {
    uint32_t tx_packets;
    uint32_t tx_bytes;
    uint32_t tx_reliable_packets;
    uint32_t tx_best_effort_packets;
    uint32_t tx_retries;
    uint32_t tx_submit_errors;
    uint32_t tx_mac_failures;
    uint32_t rx_packets;
    uint32_t rx_bytes;
    uint32_t rx_invalid_packets;
    uint32_t rx_queue_overflows;
    uint32_t rx_duplicates;
    uint32_t ack_sent;
    uint32_t ack_received;
    uint32_t ack_timeouts;
    uint32_t late_acks;
    uint32_t unexpected_acks;
    uint32_t sequence_errors;
    uint32_t timing_errors;
};

/** 单 peer RTT 与 ACK 超时快照。 */
struct PeerMetrics {
    uint16_t last_rtt_ms;
    uint16_t smoothed_rtt_ms;
    uint16_t ack_timeout_ms;
    uint16_t recent_rtt_ms[3];
    uint8_t  rtt_sample_count;
    uint8_t  recent_rtt_count;
    uint8_t  recent_rtt_next;
};

/** @brief 创建固定队列和链路任务，并监听 WiFi 驱动启停。 */
esp_err_t init();
/** @brief 注销监听器并释放任务/队列；禁止从 Link 回调调用，须与 init/deinit 串行。 */
esp_err_t deinit();
/** @return true 表示链路资源已经初始化。 */
bool      is_initialized();
/** @brief WiFi 射频已启动且 esp_now_init() 成功时返回 true。 */
bool      is_active();

/** @brief 添加或更新运行期单播 peer。 */
esp_err_t add_peer(const PeerConfig& peer);
/** @brief 删除运行期 peer，不修改 APP 层 NVS。 */
esp_err_t remove_peer(const MacAddress& address);
/** @return true 表示运行期存在指定 peer。 */
bool      has_peer(const MacAddress& address);

/**
 * @brief 异步发送 ESP-NOW Link 业务包
 *
 * 数据会复制到固定发送队列。广播自动转为明文 BEST_EFFORT；单播默认要求加密 peer。
 *
 * @return ESP_OK 已入队；ESP_ERR_INVALID_STATE 链路未激活或禁止明文；
 *         ESP_ERR_NOT_FOUND peer 不存在；ESP_ERR_NO_MEM 队列已满
 */
esp_err_t send(const MacAddress& destination, uint16_t message_id, const void* payload, size_t payload_size,
               const SendOptions& options = {}, SendCallback callback = nullptr, void* context = nullptr);

/**
 * @brief 取消此前提交的业务发送及后续重传（任务上下文）。
 * @note 不取消协议 ACK，也不撤回已经交给射频驱动的帧。作用于所有 send() 请求，
 *       包括配对报文；不修改 peer/NVS。新发送属于新代际，队满仍需调用者重试。
 *       回调在链路任务执行，结果 CANCELLED。并发发送以代际采样时刻划分。
 */
void cancel_transmissions();
/** @brief ISR 版本：只更新 DRAM 代际，禁止队列/驱动/日志操作。 */
void cancel_transmissions_from_isr();

/**
 * @brief 注册 message_id 处理函数
 * @note handler 在 espnow_link 任务上下文中执行，必须快速返回。
 */
esp_err_t register_handler(uint16_t message_id, MessageHandler handler, void* context = nullptr);
/** @brief 注销指定消息 ID 的业务处理器。 */
esp_err_t unregister_handler(uint16_t message_id);

/** @brief 设置可靠发送全局默认参数。 */
esp_err_t set_default_reliable_options(const SendOptions& options);
/** @brief 覆盖指定 peer 的 ACK 超时，范围 8~100 ms。 */
esp_err_t set_peer_ack_timeout(const MacAddress& peer, uint16_t timeout_ms);
/** @brief 获取指定 peer 的 RTT/RTO 运行指标。 */
esp_err_t get_peer_metrics(const MacAddress& peer, PeerMetrics* metrics);

uint32_t get_response_timeout_ms(const MacAddress& peer, uint8_t request_attempts = 5, uint8_t response_attempts = 5,
                                 uint16_t processing_budget_ms = 30);
uint32_t get_delivery_timeout_ms(const MacAddress& peer, uint8_t max_attempts = 5,
                                 uint16_t mac_completion_budget_ms = 250);
/** @return 指定 peer 的单信道探测等待时间，单位 ms。 */
uint16_t get_channel_probe_timeout_ms(const MacAddress& peer);

/** @brief 允许本机响应其他设备的配对请求；timeout_ms 为 0 时持续到成功或手动退出。 */
esp_err_t enter_pairing_mode(uint32_t timeout_ms = 60000);
/** @brief 取消响应式/主动配对和信道恢复；普通任务等待清理，Link 回调内仅提交取消。 */
void      leave_pairing_mode();
/** @return true 表示当前处于任一配对流程。 */
bool      is_pairing();
/** @brief 配置配对策略；仅空闲时可调用，单绑定迁移保留现有第一个 peer。 */
esp_err_t configure_pairing(const PairingConfig& config);
/** @brief 获取最近一次配对终态；无结果时 serial=0。线程安全，不阻塞。 */
void      get_pairing_result(PairingResult* result);
/** @brief 主动扫描并向可配对设备发起配对。 */
esp_err_t start_pairing();
/** @brief 异步扫描并恢复指定 peer 的信道。 */
esp_err_t recover_peer_channel(const MacAddress& peer);
/** @return true 表示信道恢复流程仍在运行。 */
bool      is_recovering_channel();
/** @return 最近一次信道恢复流程的结果。 */
esp_err_t get_channel_recovery_result();
/** @return NVS 中有效的已保存 peer 数量。 */
size_t    get_saved_peer_count();
/** @brief 按逻辑索引读取一个已保存 peer。 */
esp_err_t get_saved_peer(size_t index, SavedPeer* peer);
/** @brief 更新应用角色元数据，范围 0..31；不修改密钥/信道，不允许配对或恢复中调用。 */
esp_err_t set_saved_peer_role(const MacAddress& peer, uint8_t role);
/** @brief 删除指定已保存 peer。 */
esp_err_t remove_saved_peer(const MacAddress& address);
/** @brief 清除全部已保存 peer。 */
esp_err_t clear_saved_peers();

/** @brief 获取线程安全的 uint32_t 诊断快照。 */
void get_statistics(LinkStatistics* statistics);
/** @brief 清零本次运行诊断，不影响 peer 和发送状态。 */
void reset_statistics();

} // namespace EspNowLink

#endif
