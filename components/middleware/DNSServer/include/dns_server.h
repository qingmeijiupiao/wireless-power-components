/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 简易DNS劫持服务器，用于AP配网模式Captive Portal
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-28
 */
#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include <cstdint>

#include "esp_err.h"
#include "lwip/ip4_addr.h"

namespace DNSServer {

/** DNS默认监听端口 */
constexpr uint16_t DNS_SERVER_PORT           = 53;
/** DNS单包最大长度，UDP DNS标准包通常不超过512字节 */
constexpr uint16_t DNS_SERVER_PACKET_MAX_LEN = 512;
/** DNS任务栈大小 */
constexpr uint16_t DNS_SERVER_TASK_STACK     = 3072;
/** DNS任务优先级 */
constexpr uint8_t  DNS_SERVER_TASK_PRIO      = 3;

/**
 * @brief 启动DNS劫持服务器
 * @param captive_ip 所有域名需要解析到的IPv4地址
 * @param port 监听端口，默认53
 * @return ESP_OK成功，其他值表示失败
 */
esp_err_t start(ip4_addr_t captive_ip, uint16_t port = DNS_SERVER_PORT);

/**
 * @brief 启动DNS劫持服务器
 * @param ip0 IPv4第1段
 * @param ip1 IPv4第2段
 * @param ip2 IPv4第3段
 * @param ip3 IPv4第4段
 * @param port 监听端口，默认53
 * @return ESP_OK成功，其他值表示失败
 */
esp_err_t start(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3, uint16_t port = DNS_SERVER_PORT);

/**
 * @brief 停止DNS服务器
 * @return ESP_OK成功
 */
esp_err_t stop();

/**
 * @brief 查询DNS服务器是否运行
 * @return true正在运行，false未运行
 */
bool is_running();

} // namespace DNSServer

#endif
