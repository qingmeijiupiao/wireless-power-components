/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: WiFi管理单例类，封装ESP-IDF WiFi STA/AP模式
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-04-20
 */
#ifndef WIFI_MANAGE_H
#define WIFI_MANAGE_H

#include <cstring>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @brief IP地址联合体，支持多种访问方式
 * - addr:   32位整数形式（与lwip ip_addr兼容，网络字节序）
 * - bytes:  按字节访问 bytes[0]=第一字节
 * - octet1~4: 按八位组命名访问
 */
typedef union {
    uint32_t addr;
    uint8_t  bytes[4];
    struct {
        uint8_t octet1;
        uint8_t octet2;
        uint8_t octet3;
        uint8_t octet4;
    };
} IP_t;

/**
 * @brief MAC地址联合体，支持多种访问方式
 * - val:    64位整数形式（高2字节未使用）
 * - bytes:  按字节访问 bytes[0]=第一字节
 * - octet1~6: 按八位组命名访问
 */
typedef union {
    uint64_t val;
    uint8_t  bytes[6];
    struct {
        uint8_t octet1;
        uint8_t octet2;
        uint8_t octet3;
        uint8_t octet4;
        uint8_t octet5;
        uint8_t octet6;
    };
} MAC_t;

/**
 * @brief WiFi连接状态枚举
 */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0, /**< 未连接/未启动 */
    WIFI_STATE_STA_CONNECTED,    /**< STA模式已连接到AP并获取IP */
    WIFI_STATE_AP_ACTIVE,        /**< AP模式已启动，可接受STA连接 */
} wifi_state_t;

/** SSID最大长度 */
constexpr uint8_t WIFI_SSID_MAX_LEN       = 32;
/** 密码最大长度 */
constexpr uint8_t WIFI_PASSWORD_MAX_LEN   = 64;
/** AP模式最大允许连接数 */
constexpr uint8_t WIFI_AP_MAX_CONN        = 4;
/** STA连接超时时间(ms) */
constexpr int     WIFI_CONNECT_TIMEOUT_MS = 30000;

/**
 * @brief WiFi管理单例类
 *
 * 封装ESP-IDF WiFi驱动的初始化、STA/AP模式切换、
 * 连接/断开、扫描、状态查询等常用功能。
 *
 * 使用示例:
 *   WiFiManager::instance().init();
 *   WiFiManager::instance().connect_sta("MySSID", "MyPass", true);
 *   IP_t ip = WiFiManager::instance().get_ip();
 */
class WiFiManager {
  public:
    enum class RadioEvent : uint8_t {
        BEFORE_STOP = 0,
        AFTER_START,
    };

    using RadioEventHandler = void (*)(RadioEvent event, void* context);

    /** @brief 获取单例实例 */
    static WiFiManager& instance() {
        static WiFiManager inst;
        return inst;
    }

    /** @brief 禁止拷贝/移动构造与赋值 */
    WiFiManager(const WiFiManager&)            = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    /** @brief `WiFiManager` 接口。 */
    WiFiManager(WiFiManager&&)                 = delete;
    WiFiManager& operator=(WiFiManager&&)      = delete;

    /**
     * @brief 初始化WiFi子系统
     * @note  调用此API之前需要先初始化NVS否则会失败
     * @return esp_err_t
     */
    esp_err_t init();

    /**
     * @brief 反初始化WiFi子系统
     * 停止WiFi、释放所有资源。之后可再次调用init()重新初始化。
     * @return esp_err_t
     */
    esp_err_t deinit();

    /**
     * @brief 以STA模式连接到AP
     * @param ssid     AP的SSID
     * @param password AP的密码，空字符串表示开放网络
     * @param wait     是否阻塞等待连接结果，默认false
     * @return ESP_OK成功，ESP_ERR_TIMEOUT等待超时，其他见esp_err.h
     */
    esp_err_t connect_sta(const char* ssid, const char* password, bool wait = false);

    /**
     * @brief 仅启动 STA 无线接口，不连接基础设施 AP
     *
     * 供 ESP-NOW-only 等只需要 Wi-Fi 射频的场景使用。
     *
     * @param channel 启动后设置的主信道，范围 1~14
     */
    esp_err_t start_sta_radio(uint8_t channel = 1);

    /**
     * @brief 以AP模式启动热点
     * @param ssid     热点SSID
     * @param password 热点密码，空字符串表示开放网络
     * @param max_conn 最大允许连接的STA数量，默认4
     * @param channel  WiFi信道，默认1
     * @return esp_err_t
     */
    esp_err_t start_ap(const char* ssid, const char* password, uint8_t max_conn = WIFI_AP_MAX_CONN,
                       uint8_t channel = 1);

    /**
     * @brief 以AP+STA模式启动热点
     * @note 主要用于配网场景：AP保持可访问，STA接口用于扫描或后续连接。
     * @param ssid     热点SSID
     * @param password 热点密码，空字符串表示开放网络
     * @param max_conn 最大允许连接的STA数量，默认4
     * @param channel  WiFi信道，默认1
     * @return esp_err_t
     */
    esp_err_t start_apsta(const char* ssid, const char* password, uint8_t max_conn = WIFI_AP_MAX_CONN,
                          uint8_t channel = 1);

    /**
     * @brief 断开STA连接
     * 仅在STA/APSTA模式下有效，断开后状态变为DISCONNECTED。
     * @return esp_err_t
     */
    esp_err_t disconnect();

    /**
     * @brief 停止WiFi驱动
     * 无论当前处于何种模式，都会停止WiFi并重置状态。
     * @return esp_err_t
     */
    esp_err_t stop();

    /** @brief 获取当前WiFi状态 */
    wifi_state_t get_state() const;

    /** @brief 获取当前IP地址（STA模式下有效） */
    IP_t get_ip() const;

    /** @brief 获取 AP 接口当前 IP 地址 */
    IP_t get_ap_ip() const;

    /**
     * @brief 获取指定接口的MAC地址
     * @param ifx 接口类型，默认WIFI_IF_STA
     * @return MAC_t 联合体
     */
    MAC_t get_mac(wifi_interface_t ifx = WIFI_IF_STA) const;

    /** @brief 获取当前连接AP的RSSI信号强度（STA模式下有效） */
    int8_t get_rssi() const;

    /** @brief 判断STA是否已连接 */
    bool is_connected() const;

    /** @brief 判断WiFi子系统是否已初始化 */
    bool is_initialized() const;
    /** @brief 判断 WiFi 驱动是否已经启动 */
    bool is_started() const;

    /**
     * @brief 注册 WiFi 驱动启停监听器
     *
     * 监听器在调用 esp_wifi_stop() 前和 esp_wifi_start() 成功后同步执行，
     * 回调中不得阻塞或再次切换 WiFi 模式。
     */
    esp_err_t register_radio_listener(RadioEventHandler handler, void* context);
    /** @brief `unregister_radio_listener` 接口。 */
    esp_err_t unregister_radio_listener(RadioEventHandler handler, void* context);

    /**
     * @brief 启动AP扫描
     * @param config 扫描配置，nullptr使用默认配置
     * @param block  是否阻塞等待扫描完成，默认true
     * @return esp_err_t
     */
    esp_err_t scan_start(const wifi_scan_config_t* config = nullptr, bool block = true);

    /** @brief 停止正在进行的扫描 */
    esp_err_t scan_stop();

    /**
     * @brief 获取上次扫描发现的AP数量
     * @param num 输出AP数量
     * @return esp_err_t
     */
    esp_err_t scan_get_ap_num(uint16_t* num);

    /**
     * @brief 获取上次扫描的AP记录列表（按RSSI降序）
     * @param num     输入为缓冲区大小，输出为实际获取数量
     * @param records AP记录数组
     * @return esp_err_t
     */
    esp_err_t scan_get_ap_records(uint16_t* num, wifi_ap_record_t* records);

    /** @brief 设置WiFi省电模式 */
    esp_err_t set_power_save(wifi_ps_type_t type);
    /** @brief 获取WiFi省电模式 */
    esp_err_t get_power_save(wifi_ps_type_t* type);

    /**
     * @brief 设置WiFi信道
     * @param primary 主信道号
     * @param second  辅助信道，默认WIFI_SECOND_CHAN_NONE
     * @return esp_err_t
     */
    esp_err_t set_channel(uint8_t primary, wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE);
    /** @brief 获取当前WiFi信道 */
    esp_err_t get_channel(uint8_t* primary, wifi_second_chan_t* second);

    /** @brief 设置指定接口的带宽(20MHz/40MHz) */
    esp_err_t set_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t bw);
    /** @brief 获取指定接口的带宽 */
    esp_err_t get_bandwidth(wifi_interface_t ifx, wifi_bandwidth_t* bw);

    /**
     * @brief 设置指定接口的协议位图
     * 如 WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N
     */
    esp_err_t set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);
    /** @brief 获取指定接口的协议位图 */
    esp_err_t get_protocol(wifi_interface_t ifx, uint8_t* protocol_bitmap);

    /**
     * @brief 设置最大发射功率
     * @param power 单位0.25dBm，范围[8,84]对应2~20dBm
     */
    esp_err_t set_max_tx_power(int8_t power);
    /** @brief 获取最大发射功率 */
    esp_err_t get_max_tx_power(int8_t* power);

    /** @brief 设置国家/地区信息（信道范围、功率限制等） */
    esp_err_t set_country(const wifi_country_t* country);
    /** @brief 获取当前国家/地区信息 */
    esp_err_t get_country(wifi_country_t* country);

    /**
     * @brief 获取连接到AP的STA列表（AP模式下使用）
     * @param sta_list 输出STA列表
     * @return esp_err_t
     */
    esp_err_t ap_get_sta_list(wifi_sta_list_t* sta_list);

    /**
     * @brief 踢出AP模式下的指定STA
     * @param aid STA的关联ID，0表示踢出所有
     * @return esp_err_t
     */
    esp_err_t ap_deauth_sta(uint16_t aid);

    /**
     * @brief 直接设置STA配置（不立即连接）
     * @param ssid      SSID
     * @param password  密码
     * @param bssid_set 是否绑定指定BSSID
     * @param bssid     BSSID地址，bssid_set为true时有效
     * @return esp_err_t
     */
    esp_err_t set_sta_config(const char* ssid, const char* password, bool bssid_set = false,
                             const uint8_t* bssid = nullptr);
    /** @brief 获取当前STA配置 */
    esp_err_t get_sta_config(wifi_config_t* conf);

    /**
     * @brief 直接设置AP配置
     * @param ssid     SSID
     * @param password 密码
     * @param channel  信道
     * @param authmode 认证模式，默认WPA2_PSK
     * @param max_conn 最大连接数
     * @return esp_err_t
     */
    esp_err_t set_ap_config(const char* ssid, const char* password, uint8_t channel = 1,
                            wifi_auth_mode_t authmode = WIFI_AUTH_WPA2_PSK, uint8_t max_conn = WIFI_AP_MAX_CONN);
    /** @brief 获取当前AP配置 */
    esp_err_t get_ap_config(wifi_config_t* conf);

    /**
     * @brief 设置 AP 接口 IPv4 地址
     * @note 需要在 start_ap() 之前调用，函数会同步重启 DHCP Server。
     * @param ip AP IP 地址，gateway 也会设置为该地址
     * @param netmask 子网掩码
     * @return esp_err_t
     */
    esp_err_t set_ap_ip(IP_t ip, IP_t netmask);

    /**
     * @brief 设置指定接口的MAC地址
     * @note 只能在接口未启动时设置
     * @param ifx 接口类型
     * @param mac 6字节MAC地址
     * @return esp_err_t
     */
    esp_err_t set_mac(wifi_interface_t ifx, const uint8_t mac[6]);

  private:
    /** @brief `WiFiManager` 接口。 */
    WiFiManager();
    /** @brief `~WiFiManager` 接口。 */
    ~WiFiManager();

    /** @brief WiFi事件统一回调（静态函数，通过arg访问实例） */
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    /** @brief IP事件统一回调（静态函数，通过arg访问实例） */
    static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

    /** @brief 从netif刷新IP地址到ip_成员 */
    void      update_ip_from_netif();
    /** @brief `start_wifi_driver` 接口。 */
    esp_err_t start_wifi_driver();
    /** @brief `stop_wifi_driver` 接口。 */
    esp_err_t stop_wifi_driver();
    /** @brief `notify_radio_event` 接口。 */
    void      notify_radio_event(RadioEvent event);

    struct RadioListener {
        RadioEventHandler handler;
        void*             context;
    };

    static constexpr size_t MAX_RADIO_LISTENERS = 4;

    wifi_state_t       state_;        /**< 当前WiFi状态 */
    IP_t               ip_;           /**< 当前IP地址 */
    bool               initialized_;  /**< WiFi子系统是否已初始化 */
    bool               wifi_started_; /**< WiFi驱动是否已启动(esp_wifi_start) */
    esp_netif_t*       sta_netif_;    /**< STA接口netif句柄 */
    esp_netif_t*       ap_netif_;     /**< AP接口netif句柄 */
    EventGroupHandle_t event_group_;  /**< FreeRTOS事件组，用于连接/扫描同步 */
    RadioListener      radio_listeners_[MAX_RADIO_LISTENERS];
};

#endif
