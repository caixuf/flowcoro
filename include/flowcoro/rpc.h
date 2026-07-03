#pragma once

#include <flowcoro/core.h>
#include <flowcoro/net.h>
#include <flowcoro/when_any.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <memory>
#include <atomic>
#include <vector>

namespace flowcoro::rpc {

/**
 * @brief RPC消息结构 — 换行符分隔的 JSON over TCP
 *
 * 协议格式：每条消息是一个 JSON 对象，末尾加 '\n'，通过 TCP 传输。
 *
 * 请求示例：{"id":"1","method":"Add","params":"[1,2]","is_request":true}
 * 响应示例：{"id":"1","result":"3","is_request":false}
 */
struct RpcMessage {
    std::string id;
    std::string method;
    std::string params;   // JSON 值（可为对象/数组/标量字符串）
    std::string result;   // JSON 值
    std::string error;
    bool is_request = true;

    std::string to_json() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"id\":\"" << id << "\",";
        oss << "\"method\":\"" << method << "\"";
        if (!params.empty())  oss << ",\"params\":"   << params;
        if (!result.empty())  oss << ",\"result\":"   << result;
        if (!error.empty())   oss << ",\"error\":\""  << error << "\"";
        oss << ",\"is_request\":" << (is_request ? "true" : "false");
        oss << "}";
        return oss.str();
    }

    static RpcMessage from_json(const std::string& json) {
        RpcMessage msg;

        // 提取引号包裹的字段值
        auto extract_str = [&json](const std::string& field) -> std::string {
            std::string pat = "\"" + field + "\":\"";
            size_t s = json.find(pat);
            if (s == std::string::npos) return "";
            s += pat.size();
            size_t e = json.find('"', s);
            if (e == std::string::npos) return "";
            return json.substr(s, e - s);
        };

        // 提取非引号包裹的值（对象/数组/数字）
        auto extract_val = [&json](const std::string& field) -> std::string {
            std::string pat = "\"" + field + "\":";
            size_t s = json.find(pat);
            if (s == std::string::npos) return "";
            s += pat.size();
            // 找到下一个同级的逗号或末尾 '}'
            size_t e = json.find(",\"", s);
            if (e == std::string::npos) {
                e = json.rfind('}');
                if (e == std::string::npos) return "";
            }
            std::string val = json.substr(s, e - s);
            // 若值本身以引号开头，走 extract_str 路径
            if (!val.empty() && val.front() == '"') {
                val = val.substr(1);
                auto eq = val.find('"');
                if (eq != std::string::npos) val = val.substr(0, eq);
            }
            return val;
        };

        msg.id        = extract_str("id");
        msg.method    = extract_str("method");
        msg.error     = extract_str("error");
        msg.params    = extract_val("params");
        msg.result    = extract_val("result");
        msg.is_request = json.find("\"is_request\":true") != std::string::npos;
        return msg;
    }
};

// RPC 方法处理器类型
using RpcHandler = std::function<Task<std::string>(const std::string& params)>;

// ─────────────────────────────────────────────────────────────────────────────
// RPC 客户端 — 每次调用建立独立 TCP 连接，协程化异步 I/O
// ─────────────────────────────────────────────────────────────────────────────
class RpcClient {
    std::string       host_;
    uint16_t          port_;
    std::atomic<int>  next_id_{0};

public:
    /**
     * @param host  服务器主机名或 IP
     * @param port  服务器端口
     */
    RpcClient(const std::string& host, uint16_t port)
        : host_(host), port_(port) {}

    /** 单次 RPC 调用，返回 result JSON 字符串或错误 JSON */
    Task<std::string> call(const std::string& method,
                           const std::string& params = "{}") {
        auto& loop = net::GlobalEventLoop::get();
        net::Socket sock(&loop);
        co_await sock.connect(host_, port_);

        RpcMessage req;
        req.id         = std::to_string(next_id_.fetch_add(1, std::memory_order_relaxed));
        req.method     = method;
        req.params     = params;
        req.is_request = true;

        co_await sock.write_string(req.to_json() + "\n");

        std::string line = co_await sock.read_line();
        sock.close();

        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        auto resp = RpcMessage::from_json(line);
        if (!resp.error.empty())
            co_return "{\"error\":\"" + resp.error + "\"}";
        co_return resp.result;
    }

    /**
     * 带超时的 RPC 调用。
     * 超时时返回 {"error":"timeout"}，不影响其他调用。
     */
    Task<std::string> call_timeout(const std::string& method,
                                    const std::string& params,
                                    std::chrono::milliseconds timeout) {
        // 两个竞争任务：正常 RPC 调用 vs 超时哨兵
        auto rpc_task = call(method, params);
        auto sentinel = [timeout]() -> Task<std::string> {
            co_await sleep_for(timeout);
            co_return std::string{"{\"error\":\"timeout\"}"};
        }();

        auto [idx, any_res] = co_await when_any(std::move(rpc_task),
                                                 std::move(sentinel));
        (void)idx;
        co_return std::any_cast<std::string>(any_res);
    }

    /** 并发批量调用 — 所有请求同时发出，等待全部完成 */
    Task<std::vector<std::string>> batch_call(
        const std::vector<std::pair<std::string, std::string>>& calls) {
        std::vector<Task<std::string>> tasks;
        tasks.reserve(calls.size());
        for (const auto& [m, p] : calls)
            tasks.push_back(call(m, p));

        std::vector<std::string> results;
        results.reserve(tasks.size());
        for (auto& t : tasks)
            results.push_back(co_await t);
        co_return results;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// RPC 服务器 — 基于 net::TcpServer 的真实 TCP 实现
// ─────────────────────────────────────────────────────────────────────────────
class RpcServer {
    std::unordered_map<std::string, RpcHandler> handlers_;
    int                              port_;
    net::EventLoop*                  loop_;
    std::atomic<bool>                running_{false};
    std::unique_ptr<net::TcpServer>  tcp_server_;

public:
    /**
     * @param port  监听端口
     * @param loop  事件循环指针，nullptr 时使用全局事件循环
     */
    explicit RpcServer(int port, net::EventLoop* loop = nullptr)
        : port_(port)
        , loop_(loop ? loop : &net::GlobalEventLoop::get()) {}

    /** 注册 RPC 方法处理器 */
    void register_method(const std::string& name, RpcHandler handler) {
        handlers_[name] = std::move(handler);
    }

    /**
     * 启动服务器（协程，阻塞直到 stop() 被调用）。
     * 典型用法：在独立线程中调用 sync_wait(server.start())。
     */
    Task<void> start(const std::string& host = "0.0.0.0") {
        tcp_server_ = std::make_unique<net::TcpServer>(loop_);
        tcp_server_->set_connection_handler(
            [this](std::unique_ptr<net::Socket> sock) -> Task<void> {
                co_await handle_connection(std::move(sock));
            });

        co_await tcp_server_->listen(host, static_cast<uint16_t>(port_));
        running_.store(true, std::memory_order_release);
        std::cout << "[RpcServer] listening on " << host << ":" << port_ << std::endl;

        while (running_.load(std::memory_order_acquire)) {
            co_await sleep_for(std::chrono::milliseconds(50));
        }

        tcp_server_->stop();
        tcp_server_.reset();
        std::cout << "[RpcServer] stopped" << std::endl;
    }

    /** 请求服务器停止（线程安全） */
    void stop() { running_.store(false, std::memory_order_release); }

    /** 直接处理 JSON 请求字符串（用于测试 / 进程内调用） */
    Task<std::string> handle_request(const std::string& json_data) {
        auto req = RpcMessage::from_json(json_data);
        RpcMessage resp;
        resp.id         = req.id;
        resp.is_request = false;

        auto it = handlers_.find(req.method);
        if (it == handlers_.end()) {
            resp.error = "Method not found: " + req.method;
        } else {
            try {
                resp.result = co_await it->second(req.params);
            } catch (const std::exception& e) {
                resp.error = std::string("Handler error: ") + e.what();
            }
        }
        co_return resp.to_json();
    }

    std::vector<std::string> list_methods() const {
        std::vector<std::string> methods;
        methods.reserve(handlers_.size());
        for (const auto& [name, _] : handlers_)
            methods.push_back(name);
        return methods;
    }

private:
    Task<void> handle_connection(std::unique_ptr<net::Socket> sock) {
        net::TcpConnection conn(std::move(sock));
        while (!conn.is_closed()) {
            std::string line = co_await conn.read_line();
            if (line.empty()) break;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string resp = co_await handle_request(line);
            co_await conn.write(resp + "\n");
            co_await conn.flush();
        }
    }
};

// 便捷注册宏
#define FLOWCORO_RPC_METHOD(server, method_name, handler)                       \
    (server).register_method(#method_name,                                      \
        [](const std::string& params) -> flowcoro::Task<std::string> {          \
            co_return handler(params);                                           \
        })

} // namespace flowcoro::rpc
