#include <flowcoro.hpp>
#include <flowcoro/rpc.h>
#include <flowcoro/simple_db.h>
#include <iostream>
#include <sstream>

using namespace flowcoro;
using namespace flowcoro::rpc;
using namespace flowcoro::db;

// 演示服务：用户管理服务
class UserService {
private:
    SimpleFileDB db_;

public:
    UserService() : db_("./rpc_users_db") {}
    
    // 创建用户
    Task<std::string> create_user(const std::string& params) {
        std::cout << "📝 Creating user with params: " << params << std::endl;
        
        // 简单解析参数 {"name":"Alice","email":"alice@example.com"}
        auto extract_value = [&params](const std::string& key) -> std::string {
            std::string pattern = "\"" + key + "\":\"";
            size_t start = params.find(pattern);
            if (start == std::string::npos) return "";
            start += pattern.length();
            size_t end = params.find("\"", start);
            if (end == std::string::npos) return "";
            return params.substr(start, end - start);
        };
        
        std::string name = extract_value("name");
        std::string email = extract_value("email");
        
        if (name.empty() || email.empty()) {
            co_return "{\"success\":false,\"error\":\"Missing name or email\"}";
        }
        
        // 生成用户ID
        std::string user_id = "user_" + std::to_string(std::time(nullptr));
        
        // 存储到数据库
        auto users_collection = db_.collection("users");
        SimpleDocument user_doc(user_id);
        user_doc.set("name", name);
        user_doc.set("email", email);
        user_doc.set("created_at", std::to_string(std::time(nullptr)));
        
        bool success = co_await users_collection->insert(user_doc);
        
        if (success) {
            std::ostringstream result;
            result << "{\"success\":true,\"user_id\":\"" << user_id << "\"}";
            co_return result.str();
        } else {
            co_return "{\"success\":false,\"error\":\"Failed to create user\"}";
        }
    }
    
    // 获取用户
    Task<std::string> get_user(const std::string& params) {
        std::cout << "🔍 Getting user with params: " << params << std::endl;
        
        // 解析用户ID
        auto extract_value = [&params](const std::string& key) -> std::string {
            std::string pattern = "\"" + key + "\":\"";
            size_t start = params.find(pattern);
            if (start == std::string::npos) return "";
            start += pattern.length();
            size_t end = params.find("\"", start);
            if (end == std::string::npos) return "";
            return params.substr(start, end - start);
        };
        
        std::string user_id = extract_value("user_id");
        if (user_id.empty()) {
            co_return "{\"success\":false,\"error\":\"Missing user_id\"}";
        }
        
        auto users_collection = db_.collection("users");
        auto user_doc = co_await users_collection->find_by_id(user_id);
        
        if (user_doc.id.empty()) {
            co_return "{\"success\":false,\"error\":\"User not found\"}";
        }
        
        std::ostringstream result;
        result << "{\"success\":true,\"user\":{"
               << "\"id\":\"" << user_doc.id << "\","
               << "\"name\":\"" << user_doc.get("name") << "\","
               << "\"email\":\"" << user_doc.get("email") << "\","
               << "\"created_at\":\"" << user_doc.get("created_at") << "\""
               << "}}";
        
        co_return result.str();
    }
    
    // 列出所有用户
    Task<std::string> list_users(const std::string& params) {
        std::cout << "📋 Listing all users" << std::endl;
        
        auto users_collection = db_.collection("users");
        auto all_users = co_await users_collection->find_all();
        
        std::ostringstream result;
        result << "{\"success\":true,\"users\":[";
        
        bool first = true;
        for (const auto& user : all_users) {
            if (!first) result << ",";
            result << "{"
                   << "\"id\":\"" << user.id << "\","
                   << "\"name\":\"" << user.get("name") << "\","
                   << "\"email\":\"" << user.get("email") << "\""
                   << "}";
            first = false;
        }
        
        result << "],\"count\":" << all_users.size() << "}";
        co_return result.str();
    }
};

// 演示服务：计算服务
class CalculatorService {
public:
    // 加法运算
    Task<std::string> add(const std::string& params) {
        std::cout << "➕ Computing addition: " << params << std::endl;
        
        // 解析参数 {"a":10,"b":20}
        auto extract_number = [&params](const std::string& key) -> double {
            std::string pattern = "\"" + key + "\":";
            size_t start = params.find(pattern);
            if (start == std::string::npos) return 0.0;
            start += pattern.length();
            size_t end = params.find(",", start);
            if (end == std::string::npos) {
                end = params.find("}", start);
            }
            if (end == std::string::npos) return 0.0;
            
            std::string num_str = params.substr(start, end - start);
            return std::stod(num_str);
        };
        
        double a = extract_number("a");
        double b = extract_number("b");
        double result = a + b;
        
        std::ostringstream oss;
        oss << "{\"success\":true,\"result\":" << result << "}";
        co_return oss.str();
    }
    
    // 获取系统状态
    Task<std::string> status(const std::string& params) {
        std::cout << "📊 Getting system status" << std::endl;
        
        std::ostringstream result;
        result << "{"
               << "\"success\":true,"
               << "\"status\":\"running\","
               << "\"timestamp\":" << std::time(nullptr) << ","
               << "\"version\":\"" << flowcoro::version() << "\""
               << "}";
        
        co_return result.str();
    }
};

// RPC演示
Task<void> rpc_demo() {
    std::cout << "🚀 FlowCoro RPC System Demo" << std::endl;
    std::cout << "============================" << std::endl;
    
    // 1. 创建RPC服务器
    std::cout << "\n📡 Setting up RPC Server..." << std::endl;
    RpcServer server(8080);
    
    // 2. 创建服务实例
    UserService user_service;
    CalculatorService calc_service;
    
    // 3. 注册RPC方法
    server.register_method("user.create", [&user_service](const std::string& params) -> Task<std::string> {
        co_return co_await user_service.create_user(params);
    });
    
    server.register_method("user.get", [&user_service](const std::string& params) -> Task<std::string> {
        co_return co_await user_service.get_user(params);
    });
    
    server.register_method("user.list", [&user_service](const std::string& params) -> Task<std::string> {
        co_return co_await user_service.list_users(params);
    });
    
    server.register_method("calc.add", [&calc_service](const std::string& params) -> Task<std::string> {
        co_return co_await calc_service.add(params);
    });
    
    server.register_method("system.status", [&calc_service](const std::string& params) -> Task<std::string> {
        co_return co_await calc_service.status(params);
    });
    
    // 4. 显示已注册的方法
    auto methods = server.list_methods();
    std::cout << "📋 Registered RPC methods (" << methods.size() << "):" << std::endl;
    for (const auto& method : methods) {
        std::cout << "  🔧 " << method << std::endl;
    }
    
    // 5. 模拟RPC调用（因为没有真正的TCP服务器，我们直接调用）
    std::cout << "\n🔄 Simulating RPC calls..." << std::endl;
    
    // 创建用户
    std::string create_user_result = co_await server.handle_request(R"({
        "id": "1",
        "method": "user.create",
        "params": {"name": "Alice Johnson", "email": "alice@example.com"},
        "is_request": true
    })");
    std::cout << "✅ user.create result: " << create_user_result << std::endl;
    
    // 计算加法
    std::string calc_result = co_await server.handle_request(R"({
        "id": "2", 
        "method": "calc.add",
        "params": {"a": 15, "b": 25},
        "is_request": true
    })");
    std::cout << "✅ calc.add result: " << calc_result << std::endl;
    
    // 获取系统状态
    std::string status_result = co_await server.handle_request(R"({
        "id": "3",
        "method": "system.status", 
        "params": {},
        "is_request": true
    })");
    std::cout << "✅ system.status result: " << status_result << std::endl;
    
    // 列出用户
    std::string list_result = co_await server.handle_request(R"({
        "id": "4",
        "method": "user.list",
        "params": {},
        "is_request": true
    })");
    std::cout << "✅ user.list result: " << list_result << std::endl;
    
    // 6. 测试错误处理
    std::cout << "\n❌ Testing error handling..." << std::endl;
    std::string error_result = co_await server.handle_request(R"({
        "id": "5",
        "method": "nonexistent.method",
        "params": {},
        "is_request": true
    })");
    std::cout << "✅ Error handling result: " << error_result << std::endl;
    
    std::cout << "\n🎯 RPC Demo completed successfully!" << std::endl;
    std::cout << "💡 Next steps: Implement TCP server for real network RPC" << std::endl;
}

int main() {
    try {
        sync_wait(rpc_demo());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RPC Demo error: " << e.what() << std::endl;
        return 1;
    }
}
