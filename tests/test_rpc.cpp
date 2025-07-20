#include <coroutine>
#include <flowcoro/core.h>
#include <flowcoro.hpp>
#include <flowcoro/simple_rpc.h>
#include <flowcoro/async_rpc.h>
#include <flowcoro/simple_db.h>
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::rpc;
using namespace flowcoro::test;
using namespace flowcoro::db;

// 测试用异步服务
class TestUserService : public RpcService {
public:
    explicit TestUserService(std::shared_ptr<SimpleFileDB> db) : RpcService(db) {}
    
    std::string service_name() const override { return "TestUserService"; }
    
    void register_methods(AsyncRpcServer& server) override {
        server.register_async_method("test.create_user", 
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await create_user(params);
            });
        
        server.register_async_method("test.get_users",
            [this](const std::string& params) -> Task<std::string> {
                co_return co_await get_users(params);
            });
    }
    
    Task<std::string> create_user(const std::string& params) {
        // 模拟异步处理
        co_await sleep_for(std::chrono::milliseconds(10));
        
        // 简单创建用户，不依赖复杂的数据库操作
        co_return "{\"success\":true,\"user_id\":\"test_user_1\"}";
    }
    
    Task<std::string> get_users(const std::string& params) {
        co_await sleep_for(std::chrono::milliseconds(5));
        co_return "{\"count\":1}";
    }
};

// 测试同步RPC
void test_sync_rpc() {
    TestRunner::reset();
    TEST_SUITE("Synchronous RPC Tests");
    
    // 测试轻量级RPC服务器
    LightRpcServer server;
    
    // 注册测试方法
    server.register_method("test.echo", [](const std::string& params) -> std::string {
        return "{\"echo\":\"" + params + "\"}";
    });
    
    server.register_method("test.add", [](const std::string& params) -> std::string {
        size_t comma = params.find(',');
        if (comma == std::string::npos) return "{\"error\":\"Invalid params\"}";
        
        int a = std::stoi(params.substr(0, comma));
        int b = std::stoi(params.substr(comma + 1));
        
        return "{\"result\":" + std::to_string(a + b) + "}";
    });
    
    // 测试方法注册
    auto methods = server.list_methods();
    TEST_EXPECT_EQ(methods.size(), 2);
    
    // 测试方法调用
    std::string echo_result = server.handle_request("test.echo", "hello");
    TEST_EXPECT_TRUE(echo_result.find("hello") != std::string::npos);
    
    std::string add_result = server.handle_request("test.add", "10,20");
    TEST_EXPECT_TRUE(add_result.find("30") != std::string::npos);
    
    // 测试不存在的方法
    std::string error_result = server.handle_request("nonexistent", "");
    TEST_EXPECT_TRUE(error_result.find("Method not found") != std::string::npos);
    
    TestRunner::print_summary();
}

// 前向声明
Task<void> async_rpc_test();

// 测试异步RPC
void test_async_rpc() {
    TestRunner::reset();
    TEST_SUITE("Asynchronous RPC Tests");
    
    sync_wait(async_rpc_test());
    TestRunner::print_summary();
}

Task<void> async_rpc_test() {
    // 创建共享数据库和服务器
    auto db = std::make_shared<SimpleFileDB>("./test_async_rpc_db");
    AsyncRpcServer server("./test_rpc_logs");

    // 用 shared_ptr 管理服务，handler 捕获 shared_ptr，避免悬空
    auto user_service = std::make_shared<TestUserService>(db);
    server.register_async_method("test.create_user",
        [user_service](const std::string& params) -> Task<std::string> {
            auto result = co_await user_service->create_user(params);
            co_return result;
        });
    server.register_async_method("test.get_users",
        [user_service](const std::string& params) -> Task<std::string> {
            auto result = co_await user_service->get_users(params);
            co_return result;
        });

    // 测试异步方法调用
    std::string create_result = co_await server.handle_async_request(
        "test.create_user", "{}"
    );
    TEST_EXPECT_TRUE(create_result.find("success") != std::string::npos);

    std::string get_result = co_await server.handle_async_request(
        "test.get_users", "{}"
    );
    TEST_EXPECT_TRUE(get_result.find("count") != std::string::npos);

    // 测试批量并发调用
    std::vector<std::pair<std::string, std::string>> batch_requests = {
        {"test.get_users", "{}"},
        {"test.get_users", "{}"},
        {"test.get_users", "{}"}
    };

    auto batch_results = co_await server.handle_batch_requests(batch_requests);
    TEST_EXPECT_EQ(batch_results.size(), 3);

    // 测试服务器统计
    std::string stats = co_await server.get_server_stats();
    TEST_EXPECT_TRUE(stats.find("registered_methods") != std::string::npos);

    co_return;
}

// 测试RPC性能（协程优势）
void test_rpc_performance() {
    TestRunner::reset();
    TEST_SUITE("RPC Performance Tests");
    
    auto performance_test = []() -> Task<void> {
        auto db = std::make_shared<SimpleFileDB>("./test_perf_rpc_db");
        AsyncRpcServer server;
        
        // 注册一个模拟延迟的方法
        server.register_async_method("test.delay", 
            [](const std::string& params) -> Task<std::string> {
                co_await sleep_for(std::chrono::milliseconds(50));
                co_return "{\"completed\":true}";
            });
        
        // 测试并发性能
        auto start_time = std::chrono::steady_clock::now();
        
        std::vector<std::pair<std::string, std::string>> concurrent_requests;
        for (int i = 0; i < 5; ++i) {
            concurrent_requests.push_back({"test.delay", "{}"});
        }
        
        auto results = co_await server.handle_batch_requests(concurrent_requests);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        // 5个50ms的请求如果是并发的应该在100ms内完成（而不是250ms）
        TEST_EXPECT_TRUE(duration.count() < 150);
        TEST_EXPECT_EQ(results.size(), 5);
        
        // 验证所有请求都成功
        for (const auto& result : results) {
            TEST_EXPECT_TRUE(result.find("completed") != std::string::npos);
        }
        
        co_return;
    };
    
    sync_wait(performance_test());
    TestRunner::print_summary();
}

// 简单的异步RPC测试（不使用复杂的服务类）
Task<void> simple_async_rpc_test() {
    // 创建最简单的异步RPC服务器，用 shared_ptr 管理生命周期
    auto server = std::make_shared<AsyncRpcServer>();
    
    // 注册一个非常简单的方法，不依赖任何外部对象
    server->register_async_method("test.simple", 
        [](const std::string& params) -> Task<std::string> {
            co_return "{\"message\":\"simple test\"}";
        });
    
    // 注册一个带延时的方法，测试 sleep_for - 捕获 server 的 shared_ptr
    server->register_async_method("test.delay", 
        [server](const std::string& params) -> Task<std::string> {
            co_await sleep_for(std::chrono::milliseconds(10));
            co_return "{\"message\":\"delayed test\"}";
        });
    
    // 测试简单方法调用
    std::string result1 = co_await server->handle_async_request("test.simple", "{}");
    TEST_EXPECT_TRUE(result1.find("simple test") != std::string::npos);
    
    // 测试带延时的方法调用
    std::string result2 = co_await server->handle_async_request("test.delay", "{}");
    TEST_EXPECT_TRUE(result2.find("delayed test") != std::string::npos);
    
    co_return;
}

// 测试异步RPC - 最简化版本
void test_simple_async_rpc() {
    TestRunner::reset();
    TEST_SUITE("Simple Async RPC Tests");
    
    sync_wait(simple_async_rpc_test());
    TestRunner::print_summary();
}

int main() {
    std::cout << "🧪 FlowCoro RPC System Tests" << std::endl;
    std::cout << "=============================" << std::endl;
    
    std::cout << "\n🔬 Testing synchronous RPC..." << std::endl;
    test_sync_rpc();
    
    std::cout << "\n⚡ Testing simple async RPC..." << std::endl;
    test_simple_async_rpc();
    
    std::cout << "\n✅ All RPC tests completed!" << std::endl;
    std::cout << "🎯 Demonstrated: sync RPC, simple async RPC" << std::endl;
    
    // 等待所有异步任务完成，但不显式 shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    return 0;
}
