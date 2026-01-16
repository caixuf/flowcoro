#pragma once

#include <flowcoro/core.h>
#include <flowcoro/network.h>
#include <flowcoro/http_client.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>
#include <memory>

namespace flowcoro::rpc {

// RPC
struct RpcMessage {
    std::string id; // ID
    std::string method; // 
    std::string params; // JSON
    std::string result; // JSON
    std::string error; // 
    bool is_request = true; // 

    // JSON
    std::string to_json() const {
        std::ostringstream oss;
        oss << "{";
        oss << "\"id\":\"" << id << "\",";
        oss << "\"method\":\"" << method << "\",";
        if (!params.empty()) {
            oss << "\"params\":" << params << ",";
        }
        if (!result.empty()) {
            oss << "\"result\":" << result << ",";
        }
        if (!error.empty()) {
            oss << "\"error\":\"" << error << "\",";
        }
        oss << "\"is_request\":" << (is_request ? "true" : "false");
        oss << "}";
        return oss.str();
    }

    // JSON
    static RpcMessage from_json(const std::string& json) {
        RpcMessage msg;

        // JSONJSON
        auto extract_field = [&json](const std::string& field) -> std::string {
            std::string pattern = "\"" + field + "\":\"";
            size_t start = json.find(pattern);
            if (start == std::string::npos) return "";
            start += pattern.length();
            size_t end = json.find("\"", start);
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
        };

        msg.id = extract_field("id");
        msg.method = extract_field("method");
        msg.error = extract_field("error");

        // paramsresult
        size_t params_pos = json.find("\"params\":");
        if (params_pos != std::string::npos) {
            params_pos += 9;
            // params
            size_t comma_pos = json.find(",", params_pos);
            if (comma_pos != std::string::npos) {
                msg.params = json.substr(params_pos, comma_pos - params_pos);
            }
        }

        msg.is_request = json.find("\"is_request\":true") != std::string::npos;

        return msg;
    }
};

// RPC
using RpcHandler = std::function<Task<std::string>(const std::string& params)>;

// RPC
class RpcClient {
private:
    net::HttpClient http_client_;
    std::string server_url_;
    int request_id_counter_ = 0;

public:
    RpcClient(const std::string& server_url) : server_url_(server_url) {}

    //  - 
    Task<std::string> call(const std::string& method, const std::string& params = "{}") {
        RpcMessage request;
        request.id = std::to_string(++request_id_counter_);
        request.method = method;
        request.params = params;
        request.is_request = true;

        std::string json_data = request.to_json();

        // 
        std::string url = server_url_ + "/rpc";
        std::unordered_map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";

        auto response = co_await http_client_.post(url, json_data, headers);

        if (!response.success) {
            std::string error_msg = "{\"error\":\"Network error: " + response.error_message + "\"}";
            co_return error_msg;
        }

        auto rpc_response = RpcMessage::from_json(response.body);
        if (!rpc_response.error.empty()) {
            std::string error_msg = "{\"error\":\"" + rpc_response.error + "\"}";
            co_return error_msg;
        }

        co_return rpc_response.result;
    }

    // 
    Task<std::vector<std::string>> batch_call(
        const std::vector<std::pair<std::string, std::string>>& calls
    ) {
        std::vector<Task<std::string>> tasks;

        for (const auto& [method, params] : calls) {
            tasks.push_back(call(method, params));
        }

        std::vector<std::string> results;
        results.reserve(tasks.size());

        for (auto& task : tasks) {
            results.push_back(co_await task);
        }

        co_return results;
    }
};

// RPC
class RpcServer {
private:
    std::unordered_map<std::string, RpcHandler> handlers_;
    int port_;
    bool running_ = false;

public:
    RpcServer(int port) : port_(port) {}

    // RPC
    void register_method(const std::string& method_name, RpcHandler handler) {
        handlers_[method_name] = std::move(handler);
    }

    // RPC
    Task<std::string> handle_request(const std::string& json_data) {
        auto request = RpcMessage::from_json(json_data);

        RpcMessage response;
        response.id = request.id;
        response.is_request = false;

        auto handler_it = handlers_.find(request.method);
        if (handler_it == handlers_.end()) {
            response.error = "Method not found: " + request.method;
            co_return response.to_json();
        }

        try {
            response.result = co_await handler_it->second(request.params);
        } catch (const std::exception& e) {
            response.error = std::string("Handler error: ") + e.what();
        }

        co_return response.to_json();
    }

    // TCP
    Task<void> start() {
        running_ = true;
        std::cout << " RPC Server started on port " << port_ << std::endl;

        // TCP
        // 
        while (running_) {
            // TCP
            // handle_request
            co_await sleep_for(std::chrono::milliseconds(100));
        }
        co_return;
    }

    void stop() {
        running_ = false;
        std::cout << " RPC Server stopped" << std::endl;
    }

    // 
    std::vector<std::string> list_methods() const {
        std::vector<std::string> methods;
        methods.reserve(handlers_.size());

        for (const auto& [method, _] : handlers_) {
            methods.push_back(method);
        }

        return methods;
    }
};

// RPC
#define FLOWCORO_RPC_METHOD(server, method_name, handler) \
    server.register_method(#method_name, [](const std::string& params) -> Task<std::string> { \
        co_return handler(params); \
    })

// RPC
template<typename T>
class RpcProxy {
private:
    RpcClient& client_;

public:
    explicit RpcProxy(RpcClient& client) : client_(client) {}

    // 
    // 
    Task<std::string> invoke(const std::string& method, const std::string& params = "{}") {
        co_return co_await client_.call(method, params);
    }
};

} // namespace flowcoro::rpc
