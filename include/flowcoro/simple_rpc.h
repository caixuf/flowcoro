#pragma once

#include <flowcoro/core.h>
#include <unordered_map>
#include <functional>
#include <string>
#include <sstream>

namespace flowcoro::rpc {

// RPC
struct SimpleRpcMessage {
    std::string id;
    std::string method;
    std::string params;
    std::string result;
    std::string error;

    std::string to_json() const {
        std::ostringstream oss;
        oss << "{\"id\":\"" << id << "\",\"method\":\"" << method << "\"";
        if (!params.empty()) oss << ",\"params\":" << params;
        if (!result.empty()) oss << ",\"result\":" << result;
        if (!error.empty()) oss << ",\"error\":\"" << error << "\"";
        oss << "}";
        return oss.str();
    }
};

// RPC
using SimpleRpcHandler = std::function<std::string(const std::string&)>;

// RPC
class LightRpcServer {
private:
    std::unordered_map<std::string, SimpleRpcHandler> handlers_;

public:
    void register_method(const std::string& name, SimpleRpcHandler handler) {
        handlers_[name] = handler;
    }

    std::string handle_request(const std::string& method, const std::string& params) {
        auto it = handlers_.find(method);
        if (it == handlers_.end()) {
            return "{\"error\":\"Method not found\"}";
        }

        try {
            return it->second(params);
        } catch (const std::exception& e) {
            return "{\"error\":\"" + std::string(e.what()) + "\"}";
        }
    }

    std::vector<std::string> list_methods() const {
        std::vector<std::string> methods;
        for (const auto& [method, _] : handlers_) {
            methods.push_back(method);
        }
        return methods;
    }
};

} // namespace flowcoro::rpc
