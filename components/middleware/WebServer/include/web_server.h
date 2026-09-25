/*
 * @version: 1.0
 * @LastEditors: qingmeijiupiao
 * @Description: 轻量级 HTTP WebServer 中间件，封装 ESP-IDF esp_http_server
 * @Author: qingmeijiupiao
 * @LastEditTime: 2026-06-06 16:13:19
 */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <cstddef>
#include <cstdint>
#include <functional>

#include "esp_err.h"
#include "esp_http_server.h"

namespace WebServer {

/** 路由表最大数量，静态分配，避免运行期扩容 */
constexpr uint8_t  WEB_SERVER_MAX_ROUTES           = 64;
/** 全局中间件最大数量，按注册顺序执行 */
constexpr uint8_t  WEB_SERVER_MAX_MIDDLEWARES      = 8;
/** URI缓存长度，不含query字符串 */
constexpr uint16_t WEB_SERVER_URI_MAX_LEN          = 96;
/** query字符串缓存长度 */
constexpr uint16_t WEB_SERVER_QUERY_MAX_LEN        = 128;
/** 请求体最大缓存长度，超过该长度返回413 */
constexpr uint16_t WEB_SERVER_BODY_MAX_LEN         = 1024;
/** 预留给业务层读取Header时使用的建议长度 */
constexpr uint16_t WEB_SERVER_HEADER_VALUE_MAX_LEN = 128;
/** 客户端 IPv4 文本缓存长度，包含结尾 NUL。 */
constexpr uint8_t  WEB_SERVER_PEER_IP_MAX_LEN      = 16;

/** HTTP方法枚举，ANY用于业务路由匹配任意方法 */
enum class Method : uint8_t {
    GET,
    POST,
    PUT,
    DELETE_,
    PATCH,
    OPTIONS,
    HEAD,
    ANY,
};

/**
 * @brief 请求上下文
 *
 * handler和middleware通过该结构访问当前HTTP请求。
 *
 * @note 该结构体只在handler/middleware执行期间有效，不要保存指针到异步任务中使用。
 */
struct Request {
    httpd_req_t* raw                                 = nullptr;
    Method       method                              = Method::GET;
    char         uri[WEB_SERVER_URI_MAX_LEN]         = {};
    char         query[WEB_SERVER_QUERY_MAX_LEN]     = {};
    char         peer_ip[WEB_SERVER_PEER_IP_MAX_LEN] = {};
    char         body[WEB_SERVER_BODY_MAX_LEN + 1]   = {};
    size_t       body_len                            = 0;
    bool         body_loaded                         = false;
};

using Handler          = std::function<esp_err_t(Request* request)>;
using Middleware       = std::function<esp_err_t(Request* request)>;
using BodyChunkHandler = std::function<esp_err_t(const char* data, size_t size)>;

/**
 * @brief 初始化WebServer模块
 *
 * 初始化静态路由表、中间件表和监听端口。重复调用不会重复初始化。
 *
 * @param port HTTP监听端口，默认80。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t init(uint16_t port = 80);

/**
 * @brief 启动HTTP服务器
 *
 * 启动底层esp_http_server，并注册各HTTP方法的统一分发入口。
 *
 * @return ESP_OK成功，其他值表示失败。
 * @note 建议在begin之前完成路由和中间件注册。
 */
esp_err_t begin();

/**
 * @brief 停止HTTP服务器
 *
 * 停止底层esp_http_server，但保留已注册的路由和中间件。
 *
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t stop();

/**
 * @brief 反初始化WebServer模块
 *
 * 停止HTTP服务器并清空路由表、中间件表和404处理函数。
 *
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t deinit();

/**
 * @brief 注册路由处理函数
 *
 * 将指定URI和HTTP方法绑定到handler。重复注册同一URI和方法时覆盖旧handler。
 *
 * @param uri 访问路径，例如"/api/state"。
 * @param method HTTP方法。
 * @param handler 请求处理函数。
 * @return ESP_OK成功，ESP_ERR_INVALID_STATE表示服务器已启动，其他值表示失败。
 * @note 当前实现要求路由必须在begin之前注册。
 */
esp_err_t on(const char* uri, Method method, Handler handler);

/**
 * @brief 注册全局中间件
 *
 * 中间件按注册顺序执行，可用于日志、鉴权、CORS等公共处理。
 *
 * @param middleware 中间件函数。
 * @return ESP_OK成功，ESP_ERR_INVALID_STATE表示服务器已启动，其他值表示失败。
 * @note 中间件返回非ESP_OK时，请求处理链停止。
 */
esp_err_t use(Middleware middleware);

/**
 * @brief 设置404处理函数
 *
 * 当请求没有匹配到任何路由，且未开启Captive Portal兜底时调用该函数。
 *
 * @param handler 未匹配路由时调用的处理函数。
 */
void on_not_found(Handler handler);

/**
 * @brief 开关配网页面兜底模式
 *
 * 开启后，未匹配到路由的请求会回落到"/"路由，常用于Captive Portal弹窗。
 *
 * @param enable true开启，false关闭。
 */
void enable_captive_portal(bool enable);

/**
 * @brief 查询HTTP服务器是否正在运行
 *
 * @return true表示正在运行，false表示未运行。
 */
bool is_running();

/**
 * @brief 注册Flash内嵌静态资源
 *
 * 将固件内嵌资源注册为GET路由，适合返回HTML、CSS、JS和图片等静态文件。
 *
 * @param uri 访问路径。
 * @param data 静态资源起始地址。
 * @param size 静态资源长度。
 * @param content_type MIME类型。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t serve_static(const char* uri, const char* data, size_t size, const char* content_type);

/**
 * @brief 发送HTTP响应
 *
 * 设置HTTP状态码和Content-Type后发送响应数据。
 *
 * @param request 请求上下文。
 * @param status_code HTTP状态码。
 * @param content_type MIME类型。
 * @param data 响应数据，size为0时可传nullptr。
 * @param size 响应数据长度。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t send(Request* request, int status_code, const char* content_type, const char* data, size_t size);

/** Send a gzip-compressed HTTP response. */
esp_err_t send_gzip(Request* request, int status_code, const char* content_type, const char* data, size_t size);

/**
 * @brief 发送text/plain响应
 *
 * @param request 请求上下文。
 * @param text 以'\0'结尾的文本内容。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t send_text(Request* request, const char* text);

/**
 * @brief 发送text/html响应
 *
 * @param request 请求上下文。
 * @param html HTML数据起始地址。
 * @param size HTML数据长度。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t send_html(Request* request, const char* html, size_t size);

/** Send gzip-compressed HTML. */
esp_err_t send_html_gzip(Request* request, const char* html, size_t size);

/**
 * @brief 发送application/json响应
 *
 * @param request 请求上下文。
 * @param json 以'\0'结尾的JSON字符串。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t send_json(Request* request, const char* json);

/**
 * @brief 发送302重定向响应
 *
 * @param request 请求上下文。
 * @param location 重定向目标地址。
 * @return ESP_OK成功，其他值表示失败。
 */
esp_err_t redirect(Request* request, const char* location);

/**
 * @brief 读取请求Header
 *
 * @param request 请求上下文。
 * @param key Header名称。
 * @param value 输出缓冲区。
 * @param value_size 输出缓冲区长度。
 * @return ESP_OK成功，其他值表示Header不存在或读取失败。
 */
esp_err_t get_header(Request* request, const char* key, char* value, size_t value_size);

/**
 * @brief 从query字符串中读取指定key的值
 *
 * @param request 请求上下文。
 * @param key query参数名称。
 * @param value 输出缓冲区。
 * @param value_size 输出缓冲区长度。
 * @return ESP_OK成功，其他值表示参数不存在或读取失败。
 */
esp_err_t get_query_value(Request* request, const char* key, char* value, size_t value_size);

/**
 * @brief 读取请求体到Request::body
 *
 * 从底层httpd_req_t读取body，并缓存到Request::body中。
 *
 * @param request 请求上下文。
 * @return ESP_OK成功，ESP_ERR_NO_MEM表示请求体超过缓存上限，其他值表示读取失败。
 * @note 请求体最大长度由WEB_SERVER_BODY_MAX_LEN限制。
 */
esp_err_t load_body(Request* request);

/**
 * @brief 流式读取请求体，并将数据块交给调用方处理。
 *
 * 适合固件上传等大请求体。数据不会写入 Request::body，调用方应提供固定缓冲，
 * 并在 chunk_handler 中同步消费数据。连续接收超时或连接断开时停止读取。
 *
 * @param request 请求上下文。
 * @param buffer 分块接收缓冲区。
 * @param buffer_size 缓冲区长度。
 * @param chunk_handler 每次收到数据后的同步处理函数。
 * @return ESP_OK 成功，其他值表示参数、网络接收或业务处理失败。
 */
esp_err_t stream_body(Request* request, char* buffer, size_t buffer_size, BodyChunkHandler chunk_handler);

} // namespace WebServer

#endif
