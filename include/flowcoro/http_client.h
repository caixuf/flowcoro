#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <sstream>
#include <regex>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include "core.h"

namespace flowcoro::net {

// HTTP方法
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD,
    OPTIONS
};

// HTTP响应状态
struct HttpResponse {
    int status_code = 0;
    std::string status_text;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    bool success = false;
    std::string error_message;

    HttpResponse() = default;
    HttpResponse(int code, const std::string& text, const std::string& content)
        : status_code(code), status_text(text), body(content), success(code >= 200 && code < 300) {}
};

// URL解析结果
struct ParsedUrl {
    std::string scheme; // http/https
    std::string host; // 主机名
    std::string port; // 端口
    std::string path; // 路径
    std::string query; // 查询参数
    bool valid = false;
};

// HTTP请求客户端
class HttpClient {
private:
    static constexpr int DEFAULT_TIMEOUT_MS = 30000;
    static constexpr size_t MAX_RESPONSE_SIZE = 10 * 1024 * 1024; // 10MB

    // 网络初始化（Windows）
    class NetworkInit {
    public:
        NetworkInit() {
#ifdef _WIN32
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        }

        ~NetworkInit() {
#ifdef _WIN32
            WSACleanup();
#endif
        }
    };

    static NetworkInit network_init_;

    // 解析URL
    ParsedUrl parse_url(const std::string& url) {
        ParsedUrl result;

        // 简单的URL解析正则表达式
        std::regex url_regex(R"((https?)://([^/:]+)(?::(\d+))?(/[^?#]*)?(?:\?([^#]*))?(?:#.*)?)");
        std::smatch matches;

        if (std::regex_match(url, matches, url_regex)) {
            result.scheme = matches[1].str();
            result.host = matches[2].str();
            result.port = matches[3].str();
            result.path = matches[4].str();
            result.query = matches[5].str();

            // 设置默认端口
            if (result.port.empty()) {
                result.port = (result.scheme == "https") ? "443" : "80";
            }

            // 设置默认路径
            if (result.path.empty()) {
                result.path = "/";
            }

            result.valid = true;
        }

        return result;
    }

    // 创建TCP连接
    int create_connection(const std::string& host, const std::string& port) {
        struct addrinfo hints = {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        struct addrinfo* result = nullptr;
        int status = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
        if (status != 0) {
            return -1;
        }

        int sock = -1;
        for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
            sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sock == -1) continue;

            if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0) {
                break; // 连接成功
            }

#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            sock = -1;
        }

        freeaddrinfo(result);
        return sock;
    }

    // 设置socket超时
    bool set_socket_timeout(int sock, int timeout_ms) {
#ifdef _WIN32
        DWORD timeout = timeout_ms;
        return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                         reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == 0;
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
    }

    // 发送HTTP请求
    bool send_request(int sock, const std::string& method, const ParsedUrl& url,
                     const std::unordered_map<std::string, std::string>& headers,
                     const std::string& body) {
        std::ostringstream request;
        request << method << " " << url.path;
        if (!url.query.empty()) {
            request << "?" << url.query;
        }
        request << " HTTP/1.1\r\n";

        // 默认头部
        request << "Host: " << url.host << "\r\n";
        request << "Connection: close\r\n";
        request << "User-Agent: FlowCoro-HttpClient/2.0\r\n";

        if (!body.empty()) {
            request << "Content-Length: " << body.length() << "\r\n";
        }

        // 用户自定义头部
        for (const auto& [key, value] : headers) {
            request << key << ": " << value << "\r\n";
        }

        request << "\r\n";

        if (!body.empty()) {
            request << body;
        }

        std::string request_str = request.str();
        size_t total_sent = 0;

        while (total_sent < request_str.length()) {
            ssize_t sent = send(sock, request_str.c_str() + total_sent,
                              request_str.length() - total_sent, 0);
            if (sent <= 0) {
                return false;
            }
            total_sent += sent;
        }

        return true;
    }

    // 接收HTTP响应
    HttpResponse receive_response(int sock) {
        HttpResponse response;
        std::string raw_response;
        char buffer[4096];

        // 接收数据
        while (raw_response.size() < MAX_RESPONSE_SIZE) {
            ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
            if (received <= 0) {
                break; // 连接关闭或错误
            }

            buffer[received] = '\0';
            raw_response.append(buffer, received);

            // 检查是否接收完整（简单检查Content-Length或连接关闭）
            if (raw_response.find("\r\n\r\n") != std::string::npos) {
                // 找到头部结束标志，检查是否有Content-Length
                size_t header_end = raw_response.find("\r\n\r\n") + 4;
                std::string headers_part = raw_response.substr(0, header_end);

                std::regex content_length_regex(R"(Content-Length:\s*(\d+))", std::regex_constants::icase);
                std::smatch matches;

                if (std::regex_search(headers_part, matches, content_length_regex)) {
                    size_t content_length = std::stoull(matches[1].str());
                    size_t body_length = raw_response.length() - header_end;

                    if (body_length >= content_length) {
                        break; // 接收完整
                    }
                } else {
                    // 没有Content-Length，依赖连接关闭
                    continue;
                }
            }
        }

        // 解析响应
        return parse_response(raw_response);
    }

    // 解析HTTP响应
    HttpResponse parse_response(const std::string& raw_response) {
        HttpResponse response;

        if (raw_response.empty()) {
            response.error_message = "Empty response";
            return response;
        }

        // 分离头部和主体
        size_t header_end = raw_response.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            response.error_message = "Invalid HTTP response format";
            return response;
        }

        std::string headers_part = raw_response.substr(0, header_end);
        response.body = raw_response.substr(header_end + 4);

        // 解析状态行
        std::istringstream header_stream(headers_part);
        std::string status_line;
        if (!std::getline(header_stream, status_line)) {
            response.error_message = "No status line";
            return response;
        }

        // 移除\r
        if (!status_line.empty() && status_line.back() == '\r') {
            status_line.pop_back();
        }

        // 解析状态行: "HTTP/1.1 200 OK"
        std::regex status_regex(R"(HTTP/\d\.\d\s+(\d+)\s+(.*))");
        std::smatch matches;
        if (std::regex_match(status_line, matches, status_regex)) {
            response.status_code = std::stoi(matches[1].str());
            response.status_text = matches[2].str();
        } else {
            response.error_message = "Invalid status line: " + status_line;
            return response;
        }

        // 解析头部
        std::string header_line;
        while (std::getline(header_stream, header_line)) {
            // 移除\r
            if (!header_line.empty() && header_line.back() == '\r') {
                header_line.pop_back();
            }

            if (header_line.empty()) break;

            size_t colon_pos = header_line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = header_line.substr(0, colon_pos);
                std::string value = header_line.substr(colon_pos + 1);

                // 去除前后空格
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                response.headers[key] = value;
            }
        }

        response.success = (response.status_code >= 200 && response.status_code < 300);
        return response;
    }

public:
    // GET请求
    Task<HttpResponse> get(const std::string& url,
                          const std::unordered_map<std::string, std::string>& headers = {}) {
        co_return request_sync(HttpMethod::GET, url, headers, "");
    }

    // POST请求
    Task<HttpResponse> post(const std::string& url, const std::string& body,
                           const std::unordered_map<std::string, std::string>& headers = {}) {
        auto merged_headers = headers;
        if (merged_headers.find("Content-Type") == merged_headers.end()) {
            merged_headers["Content-Type"] = "application/json";
        }
        co_return request_sync(HttpMethod::POST, url, merged_headers, body);
    }

    // 同步请求方法
    HttpResponse request_sync(HttpMethod method, const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers = {},
                             const std::string& body = "") {
        // 解析URL
        ParsedUrl parsed_url = parse_url(url);
        if (!parsed_url.valid) {
            HttpResponse error_response;
            error_response.error_message = "Invalid URL: " + url;
            return error_response;
        }

        // HTTPS支持检查
        if (parsed_url.scheme == "https") {
            HttpResponse error_response;
            error_response.error_message = "HTTPS not supported in this simple implementation";
            return error_response;
        }

        // 创建连接
        int sock = create_connection(parsed_url.host, parsed_url.port);
        if (sock == -1) {
            HttpResponse error_response;
            error_response.error_message = "Failed to connect to " + parsed_url.host + ":" + parsed_url.port;
            return error_response;
        }

        // 设置超时
        set_socket_timeout(sock, DEFAULT_TIMEOUT_MS);

        // 发送请求
        std::string method_str;
        switch (method) {
            case HttpMethod::GET: method_str = "GET"; break;
            case HttpMethod::POST: method_str = "POST"; break;
            case HttpMethod::PUT: method_str = "PUT"; break;
            case HttpMethod::DELETE: method_str = "DELETE"; break;
            case HttpMethod::PATCH: method_str = "PATCH"; break;
            case HttpMethod::HEAD: method_str = "HEAD"; break;
            case HttpMethod::OPTIONS: method_str = "OPTIONS"; break;
        }

        bool send_success = send_request(sock, method_str, parsed_url, headers, body);
        if (!send_success) {
            HttpResponse error_response;
            error_response.error_message = "Failed to send request";
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return error_response;
        }

        // 接收响应
        HttpResponse result = receive_response(sock);

        // 关闭连接
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif

        return result;
    }

    // 通用请求方法（协程版本）
    Task<HttpResponse> request(HttpMethod method, const std::string& url,
                              const std::unordered_map<std::string, std::string>& headers = {},
                              const std::string& body = "") {
        co_return request_sync(method, url, headers, body);
    }
};

// 静态成员初始化
HttpClient::NetworkInit HttpClient::network_init_;

} // namespace flowcoro::net
