#pragma once

#include <flowcoro/core.h>
#include <flowcoro/simple_db.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <vector>
#include <memory>

namespace flowcoro::rpc {

// 支持协程的RPC处理器
using AsyncRpcHandler = std::function<Task<std::string>(const std::string&)>;

// RPC请求上下文
struct RpcContext {
    std::string request_id;
    std::string method;
    std::string params;
    std::chrono::steady_clock::time_point start_time;
    
    RpcContext(const std::string& id, const std::string& m, const std::string& p)
        : request_id(id), method(m), params(p), start_time(std::chrono::steady_clock::now()) {}
};

// 协程RPC服务器
class AsyncRpcServer {
private:
    std::unordered_map<std::string, AsyncRpcHandler> handlers_;
    std::shared_ptr<db::SimpleFileDB> db_;
    std::atomic<int> request_counter_{0};

public:
    AsyncRpcServer(const std::string& db_path = "./rpc_server_db") 
        : db_(std::make_shared<db::SimpleFileDB>(db_path)) {}
    
    // 析构函数确保清理
    ~AsyncRpcServer() {
        handlers_.clear();
        db_.reset();
    }
    
    // 注册异步RPC方法
    void register_async_method(const std::string& name, AsyncRpcHandler handler) {
        handlers_[name] = std::move(handler);
    }
    
    // 处理异步RPC请求
    Task<std::string> handle_async_request(const std::string& method, const std::string& params) {
        std::string request_id = "req_" + std::to_string(request_counter_.fetch_add(1));
        RpcContext ctx(request_id, method, params);
        
        std::string result;
        try {
            auto handler_it = handlers_.find(method);
            if (handler_it == handlers_.end()) {
                result = "{\"error\":\"Method not found: " + method + "\"}";
            } else {
                // 异步调用处理器
                result = co_await handler_it->second(params);
            }
        } catch (const std::exception& e) {
            result = "{\"error\":\"Handler error: " + std::string(e.what()) + "\"}";
        } catch (...) {
            result = "{\"error\":\"Unknown error occurred\"}";
        }
        
        co_return result;
    }
    
    // 批量处理多个请求（并发）
    Task<std::vector<std::string>> handle_batch_requests(
        const std::vector<std::pair<std::string, std::string>>& requests
    ) {
        std::vector<Task<std::string>> tasks;
        tasks.reserve(requests.size());
        
        // 并发启动所有请求
        for (const auto& [method, params] : requests) {
            tasks.push_back(handle_async_request(method, params));
        }
        
        // 等待所有请求完成
        std::vector<std::string> results;
        results.reserve(tasks.size());
        
        for (auto& task : tasks) {
            results.push_back(co_await task);
        }
        
        co_return results;
    }
    
    // 获取服务器状态
    Task<std::string> get_server_stats() {
        std::ostringstream oss;
        oss << "{"
            << "\"total_requests\":" << request_counter_.load() << ","
            << "\"registered_methods\":" << handlers_.size() << ","
            << "\"methods\":[";
        
        bool first = true;
        for (const auto& [method, _] : handlers_) {
            if (!first) oss << ",";
            oss << "\"" << method << "\"";
            first = false;
        }
        
        oss << "]"
            << "}";
        
        co_return oss.str();
    }
    
private:
    // 记录请求开始
    Task<void> log_request_start(const RpcContext& ctx) {
        auto logs_collection = db_->collection("request_logs");
        
        db::SimpleDocument log_doc(ctx.request_id);
        log_doc.set("method", ctx.method);
        log_doc.set("params", ctx.params);
        log_doc.set("status", "started");
        log_doc.set("start_time", std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                ctx.start_time.time_since_epoch()).count()));
        
        co_await logs_collection->insert(log_doc);
    }
    
    // 记录请求完成
    Task<void> log_request_complete(const RpcContext& ctx, const std::string& result) {
        auto logs_collection = db_->collection("request_logs");
        
        auto log_doc = co_await logs_collection->find_by_id(ctx.request_id);
        if (!log_doc.id.empty()) {
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - ctx.start_time).count();
            
            log_doc.set("status", "completed");
            log_doc.set("result_size", std::to_string(result.size()));
            log_doc.set("duration_ms", std::to_string(duration));
            log_doc.set("end_time", std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time.time_since_epoch()).count()));
            
            co_await logs_collection->insert(log_doc); // 重新插入更新的文档
        }
    }
};

// RPC服务基类
class RpcService {
protected:
    std::shared_ptr<db::SimpleFileDB> db_;
    
public:
    explicit RpcService(std::shared_ptr<db::SimpleFileDB> db) : db_(db) {}
    virtual ~RpcService() = default;
    
    // 获取服务名称
    virtual std::string service_name() const = 0;
    
    // 注册服务方法到RPC服务器
    virtual void register_methods(AsyncRpcServer& server) = 0;
};

// 宏定义，简化异步RPC方法注册
#define REGISTER_ASYNC_RPC_METHOD(server, service, method_name) \
    server.register_async_method(#method_name, \
        [this](const std::string& params) -> Task<std::string> { \
            co_return co_await method_name(params); \
        })

} // namespace flowcoro::rpc
