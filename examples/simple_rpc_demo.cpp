#include <flowcoro/core.h>
#include <flowcoro/simple_rpc.h>
#include <flowcoro/async_rpc.h>
#include <flowcoro/simple_db.h>
#include <iostream>
#include <chrono>
#include <thread>

using namespace flowcoro;
using namespace flowcoro::rpc;
using namespace flowcoro::db;

// 简单的延时awaitable
struct SleepAwaitable {
    std::chrono::milliseconds duration;
    
    explicit SleepAwaitable(std::chrono::milliseconds d) : duration(d) {}
    
    bool await_ready() const noexcept { return false; }
    
    void await_suspend(std::coroutine_handle<> h) const {
        std::thread([h, d = duration]() {
            std::this_thread::sleep_for(d);
            h.resume();
        }).detach();
    }
    
    void await_resume() const noexcept {}
};

// 工具函数：创建延时
SleepAwaitable coro_sleep(std::chrono::milliseconds ms) {
    return SleepAwaitable(ms);
}

// 用户管理服务（支持协程）
class UserService : public RpcService {
public:
    explicit UserService(std::shared_ptr<SimpleFileDB> db) : RpcService(db) {}
    
    std::string service_name() const override { return "UserService"; }
    
    void register_methods(AsyncRpcServer& server) override {
        server.register_async_method("user.create", 
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await create_user(params);
            });
        
        server.register_async_method("user.get",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await get_user(params);
            });
        
        server.register_async_method("user.list",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await list_users(params);
            });
    }
    
    // 异步创建用户
    Task<std::string> create_user(const std::string& params) {
        // 模拟异步处理（比如验证、外部API调用等）
        co_await coro_sleep(std::chrono::milliseconds(100));
        
        // 解析参数（简化JSON解析）
        std::string name = extract_json_string(params, "name");
        std::string email = extract_json_string(params, "email");
        
        if (name.empty() || email.empty()) {
            co_return "{\"success\":false,\"error\":\"Missing name or email\"}";
        }
        
        auto users_collection = db_->collection("users");
        std::string user_id = "user_" + std::to_string(std::time(nullptr));
        
        SimpleDocument user_doc(user_id);
        user_doc.set("name", name);
        user_doc.set("email", email);
        user_doc.set("created_at", std::to_string(std::time(nullptr)));
        
        bool success = co_await users_collection->insert(user_doc);
        
        std::ostringstream result;
        if (success) {
            result << "{\"success\":true,\"user_id\":\"" << user_id << "\",\"name\":\"" << name << "\"}";
        } else {
            result << "{\"success\":false,\"error\":\"Failed to create user\"}";
        }
        
        co_return result.str();
    }
    
    // 异步获取用户
    Task<std::string> get_user(const std::string& params) {
        // 模拟数据库查询延迟
        co_await coro_sleep(std::chrono::milliseconds(50));
        
        std::string user_id = extract_json_string(params, "user_id");
        if (user_id.empty()) {
            co_return "{\"success\":false,\"error\":\"Missing user_id\"}";
        }
        
        auto users_collection = db_->collection("users");
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
    
    // 异步列出用户
    Task<std::string> list_users(const std::string& params) {
        co_await coro_sleep(std::chrono::milliseconds(75));
        
        auto users_collection = db_->collection("users");
        auto all_users = co_await users_collection->find_all();
        
        std::ostringstream result;
        result << "{\"success\":true,\"users\":[";
        
        bool first = true;
        for (const auto& user : all_users) {
            if (!first) result << ",";
            result << "{\"id\":\"" << user.id << "\",\"name\":\"" << user.get("name") << "\"}";
            first = false;
        }
        
        result << "],\"count\":" << all_users.size() << "}";
        co_return result.str();
    }
    
private:
    std::string extract_json_string(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\":\"";
        size_t start = json.find(pattern);
        if (start == std::string::npos) return "";
        start += pattern.length();
        size_t end = json.find("\"", start);
        if (end == std::string::npos) return "";
        return json.substr(start, end - start);
    }
};

// 计算服务（支持协程）
class CalculatorService : public RpcService {
public:
    explicit CalculatorService(std::shared_ptr<SimpleFileDB> db) : RpcService(db) {}
    
    std::string service_name() const override { return "CalculatorService"; }
    
    void register_methods(AsyncRpcServer& server) override {
        server.register_async_method("calc.add",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await add(params);
            });
        
        server.register_async_method("calc.fibonacci",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await fibonacci(params);
            });
        
        server.register_async_method("calc.heavy",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await heavy_computation(params);
            });
    }
    
    Task<std::string> add(const std::string& params) {
        // 快速计算，但仍然是异步的
        co_await coro_sleep(std::chrono::milliseconds(10));
        
        auto [a, b] = parse_two_numbers(params);
        double result = a + b;
        
        co_return "{\"result\":" + std::to_string(result) + "}";
    }
    
    Task<std::string> fibonacci(const std::string& params) {
        int n = std::stoi(extract_number(params, "n"));
        
        // 异步计算斐波那契数列
        long long result = co_await calculate_fibonacci_async(n);
        
        co_return "{\"fibonacci_" + std::to_string(n) + "\":" + std::to_string(result) + "}";
    }
    
    Task<std::string> heavy_computation(const std::string& params) {
        int iterations = std::stoi(extract_number(params, "iterations"));
        if (iterations <= 0) iterations = 1000000;
        
        // 模拟重计算任务，分批处理以保持响应性
        long long sum = 0;
        int batch_size = iterations / 100;
        
        for (int i = 0; i < 100; ++i) {
            // 每个批次后让出控制权
            for (int j = 0; j < batch_size; ++j) {
                sum += i * j;
            }
            co_await coro_sleep(std::chrono::milliseconds(1));
        }
        
        co_return "{\"heavy_result\":" + std::to_string(sum) + ",\"iterations\":" + std::to_string(iterations) + "}";
    }
    
private:
    Task<long long> calculate_fibonacci_async(int n) {
        if (n <= 1) co_return n;
        
        // 对于较大的数，分步计算以保持异步性
        long long a = 0, b = 1;
        for (int i = 2; i <= n; ++i) {
            long long temp = a + b;
            a = b;
            b = temp;
            
            // 每10步让出一次控制权
            if (i % 10 == 0) {
                co_await coro_sleep(std::chrono::milliseconds(1));
            }
        }
        
        co_return b;
    }
    
    std::pair<double, double> parse_two_numbers(const std::string& params) {
        size_t comma = params.find(',');
        if (comma == std::string::npos) return {0.0, 0.0};
        
        double a = std::stod(params.substr(0, comma));
        double b = std::stod(params.substr(comma + 1));
        
        return {a, b};
    }
    
    std::string extract_number(const std::string& json, const std::string& key) {
        std::string pattern = "\"" + key + "\":";
        size_t start = json.find(pattern);
        if (start == std::string::npos) return "0";
        start += pattern.length();
        size_t end = json.find(",", start);
        if (end == std::string::npos) end = json.find("}", start);
        if (end == std::string::npos) return "0";
        return json.substr(start, end - start);
    }
};

// 协程RPC演示
Task<void> async_rpc_demo() {
    std::cout << "🚀 FlowCoro Async RPC Demo with Coroutines" << std::endl;
    std::cout << "===========================================" << std::endl;
    
    // 创建共享数据库
    auto db = std::make_shared<SimpleFileDB>("./async_rpc_db");
    
    // 创建异步RPC服务器
    AsyncRpcServer server("./rpc_server_logs");
    
    // 创建并注册服务
    UserService user_service(db);
    CalculatorService calc_service(db);
    
    user_service.register_methods(server);
    calc_service.register_methods(server);
    
    // 获取服务器统计信息
    std::string stats = co_await server.get_server_stats();
    std::cout << "\n📊 Server Stats: " << stats << std::endl;
    
    std::cout << "\n🔄 Testing async RPC calls..." << std::endl;
    
    // 单个异步调用测试
    auto start_time = std::chrono::steady_clock::now();
    
    std::string user_result = co_await server.handle_async_request(
        "user.create", 
        "{\"name\":\"Alice Johnson\",\"email\":\"alice@example.com\"}"
    );
    std::cout << "✅ user.create = " << user_result << std::endl;
    
    // 批量并发调用测试
    std::vector<std::pair<std::string, std::string>> batch_requests = {
        {"calc.add", "10,20"},
        {"calc.fibonacci", "{\"n\":10}"},
        {"user.list", "{}"},
        {"calc.add", "100,200"},
        {"calc.heavy", "{\"iterations\":100000}"}
    };
    
    std::cout << "\n⚡ Testing concurrent batch requests..." << std::endl;
    auto batch_start = std::chrono::steady_clock::now();
    
    auto batch_results = co_await server.handle_batch_requests(batch_requests);
    
    auto batch_end = std::chrono::steady_clock::now();
    auto batch_duration = std::chrono::duration_cast<std::chrono::milliseconds>(batch_end - batch_start);
    
    std::cout << "📋 Batch results (completed in " << batch_duration.count() << "ms):" << std::endl;
    for (size_t i = 0; i < batch_results.size(); ++i) {
        std::cout << "  " << (i+1) << ". " << batch_requests[i].first 
                  << " = " << batch_results[i] << std::endl;
    }
    
    // 展示异步优势：重计算任务
    std::cout << "\n🔥 Testing heavy computation (async advantage)..." << std::endl;
    auto heavy_start = std::chrono::steady_clock::now();
    
    auto heavy_result = co_await server.handle_async_request(
        "calc.heavy", 
        "{\"iterations\":1000000}"
    );
    
    auto heavy_end = std::chrono::steady_clock::now();
    auto heavy_duration = std::chrono::duration_cast<std::chrono::milliseconds>(heavy_end - heavy_start);
    
    std::cout << "💪 Heavy computation result: " << heavy_result << std::endl;
    std::cout << "⏱️  Completed in " << heavy_duration.count() << "ms (remained responsive!)" << std::endl;
    
    // 获取最终统计
    std::string final_stats = co_await server.get_server_stats();
    std::cout << "\n📈 Final Server Stats: " << final_stats << std::endl;
    
    std::cout << "\n🎯 Async RPC demo completed!" << std::endl;
    std::cout << "✨ Demonstrated: async processing, concurrent requests, responsive heavy computations" << std::endl;
}

// 简单的RPC演示（保持向后兼容）
void simple_rpc_demo() {
    std::cout << "🚀 FlowCoro Simple RPC Demo" << std::endl;
    std::cout << "===========================" << std::endl;
    
    // 创建RPC服务器
    LightRpcServer server;
    
    // 创建简单的数据库
    SimpleFileDB db("./simple_rpc_db");
    
    // 注册RPC方法
    server.register_method("echo", [](const std::string& params) -> std::string {
        return "{\"echo\":\"" + params + "\"}";
    });
    
    server.register_method("add", [](const std::string& params) -> std::string {
        // 假设params是 "10,20"
        size_t comma = params.find(',');
        if (comma == std::string::npos) {
            return "{\"error\":\"Invalid parameters\"}";
        }
        
        int a = std::stoi(params.substr(0, comma));
        int b = std::stoi(params.substr(comma + 1));
        int result = a + b;
        
        return "{\"result\":" + std::to_string(result) + "}";
    });
    
    server.register_method("time", [](const std::string& params) -> std::string {
        auto now = std::time(nullptr);
        return "{\"timestamp\":" + std::to_string(now) + "}";
    });
    
    // 显示已注册方法
    auto methods = server.list_methods();
    std::cout << "\n📋 Registered methods (" << methods.size() << "):" << std::endl;
    for (const auto& method : methods) {
        std::cout << "  🔧 " << method << std::endl;
    }
    
    // 测试RPC调用
    std::cout << "\n🔄 Testing RPC calls..." << std::endl;
    
    // Echo测试
    std::string echo_result = server.handle_request("echo", "hello world");
    std::cout << "✅ echo('hello world') = " << echo_result << std::endl;
    
    // 加法测试
    std::string add_result = server.handle_request("add", "15,25");
    std::cout << "✅ add(15,25) = " << add_result << std::endl;
    
    // 时间测试
    std::string time_result = server.handle_request("time", "");
    std::cout << "✅ time() = " << time_result << std::endl;
    
    // 错误处理测试
    std::string error_result = server.handle_request("nonexistent", "");
    std::cout << "✅ nonexistent() = " << error_result << std::endl;
    
    std::cout << "\n🎯 Simple RPC demo completed!" << std::endl;
    std::cout << "💡 This lightweight version uses minimal memory" << std::endl;
}

int main() {
    try {
        std::cout << "🚀 FlowCoro RPC Comprehensive Demo" << std::endl;
        std::cout << "====================================" << std::endl;
        
        std::cout << "\n1️⃣ Running simple synchronous RPC demo..." << std::endl;
        simple_rpc_demo();
        
        std::cout << "\n2️⃣ Running advanced asynchronous RPC demo..." << std::endl;
        sync_wait(async_rpc_demo());
        
        std::cout << "\n🎉 All RPC demos completed successfully!" << std::endl;
        std::cout << "✨ Both sync and async RPC implementations working!" << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "RPC Demo error: " << e.what() << std::endl;
        return 1;
    }
}
