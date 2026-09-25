/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 简易DNS劫持服务器实现
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-05-28
 */
#include "dns_server.h"

#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace DNSServer {

static const char* TAG = "DNSServer";

static TaskHandle_t  dns_task_handle = nullptr;
static int           dns_socket      = -1;
static ip4_addr_t    response_ip     = {};
static uint16_t      listen_port     = DNS_SERVER_PORT;
static volatile bool running         = false;

// DNS收发缓冲区静态分配，避免任务循环中动态申请内存。
static uint8_t dns_buffer[DNS_SERVER_PACKET_MAX_LEN];

/**
 * @brief 按网络字节序读取16位整数
 */
static uint16_t read_u16(const uint8_t* data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

/**
 * @brief 按网络字节序写入16位整数
 */
static void write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xff);
}

/**
 * @brief 按网络字节序写入32位整数
 */
static void write_u32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)(value & 0xff);
}

/**
 * @brief 找到DNS Question段结束位置
 * @note  当前服务器只处理普通查询名，不支持压缩指针形式的Question Name
 */
static int find_question_end(const uint8_t* packet, int length) {
    int pos = 12;
    while (pos < length) {
        uint8_t label_len = packet[pos];
        pos++;
        if ((label_len & 0xc0) != 0 || label_len > 63) {
            return -1;
        }
        if (label_len == 0) {
            break;
        }
        if (pos + label_len > length) {
            return -1;
        }
        pos += label_len;
    }

    if (pos + 4 > length) {
        return -1;
    }
    return pos + 4;
}

/**
 * @brief 在原DNS查询包基础上构造A记录响应
 * @note  用于Captive Portal，所有合法查询均返回同一个IPv4地址
 */
static int build_response(uint8_t* packet, int length) {
    if (length < 12) {
        return -1;
    }

    int question_end = find_question_end(packet, length);
    if (question_end < 0) {
        return -1;
    }

    uint16_t flags    = read_u16(packet + 2);
    uint16_t qd_count = read_u16(packet + 4);
    if (qd_count == 0) {
        return -1;
    }

    int answer_pos = question_end;
    if (answer_pos + 16 > DNS_SERVER_PACKET_MAX_LEN) {
        return -1;
    }

    // 设置响应标志位：QR=1，保留递归期望位，响应码为NoError。
    packet[2] = 0x80 | (uint8_t)(flags & 0x01);
    packet[3] = 0x00;
    write_u16(packet + 4, 1);
    write_u16(packet + 6, 1);
    write_u16(packet + 8, 0);
    write_u16(packet + 10, 0);

    // Answer Name使用压缩指针指向Question Name起始位置0x0c。
    packet[answer_pos++] = 0xc0;
    packet[answer_pos++] = 0x0c;
    write_u16(packet + answer_pos, 1);
    answer_pos += 2;
    write_u16(packet + answer_pos, 1);
    answer_pos += 2;
    write_u32(packet + answer_pos, 60);
    answer_pos += 4;
    write_u16(packet + answer_pos, 4);
    answer_pos += 2;

    // ip4_addr_get_u32返回网络序地址，可直接写入响应包。
    uint32_t addr = ip4_addr_get_u32(&response_ip);
    memcpy(packet + answer_pos, &addr, 4);
    answer_pos += 4;

    return answer_pos;
}

/**
 * @brief DNS服务任务
 * @note  阻塞接收UDP查询，解析后立即回包；SO_RCVTIMEO用于保证stop可退出
 */
static void dns_task(void* arg) {
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "socket create failed");
        running         = false;
        dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    sockaddr_in server_addr     = {};
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port        = htons(listen_port);

    int ret = bind(dns_socket, (sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "bind failed");
        closesocket(dns_socket);
        dns_socket      = -1;
        running         = false;
        dns_task_handle = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    timeval timeout = {};
    timeout.tv_sec  = 1;
    timeout.tv_usec = 0;
    setsockopt(dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    ESP_LOGI(TAG, "started on port %u", listen_port);

    while (running) {
        sockaddr_in source_addr = {};
        socklen_t   source_len  = sizeof(source_addr);
        int         len = recvfrom(dns_socket, dns_buffer, sizeof(dns_buffer), 0, (sockaddr*)&source_addr, &source_len);
        if (len <= 0) {
            continue;
        }

        int response_len = build_response(dns_buffer, len);
        if (response_len > 0) {
            sendto(dns_socket, dns_buffer, response_len, 0, (sockaddr*)&source_addr, source_len);
        }
    }

    if (dns_socket >= 0) {
        closesocket(dns_socket);
        dns_socket = -1;
    }
    dns_task_handle = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t start(ip4_addr_t captive_ip, uint16_t port) {
    if (running) {
        return ESP_OK;
    }

    response_ip = captive_ip;
    listen_port = port;
    running     = true;

    BaseType_t ok =
        xTaskCreate(dns_task, "dns_server", DNS_SERVER_TASK_STACK, nullptr, DNS_SERVER_TASK_PRIO, &dns_task_handle);
    if (ok != pdPASS) {
        running         = false;
        dns_task_handle = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t start(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3, uint16_t port) {
    ip4_addr_t ip;
    IP4_ADDR(&ip, ip0, ip1, ip2, ip3);
    return start(ip, port);
}

esp_err_t stop() {
    if (!running) {
        return ESP_OK;
    }

    running = false;
    if (dns_socket >= 0) {
        shutdown(dns_socket, 0);
    }

    uint32_t wait_count = 0;
    while (dns_task_handle != nullptr && wait_count < 50) {
        vTaskDelay(pdMS_TO_TICKS(10));
        wait_count++;
    }
    return ESP_OK;
}

bool is_running() {
    return running;
}

} // namespace DNSServer
