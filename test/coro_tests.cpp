#include "flowcoro.h"
#include "http_request_simple.h"
#include <iostream>
#include <thread>
#include <chrono>

// 简单的测试框架，替代gtest
#define EXPECT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "FAIL: " << #a << " != " << #b << " (" << (a) << " != " << (b) << ")" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " == " << #b << std::endl; \
    } \
} while(0)

#define EXPECT_TRUE(a) do { \
    if (!(a)) { \
        std::cerr << "FAIL: " << #a << " is false" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " is true" << std::endl; \
    } \
} while(0)

#define EXPECT_FALSE(a) do { \
    if ((a)) { \
        std::cerr << "FAIL: " << #a << " is true" << std::endl; \
        exit(1); \
    } else { \
        std::cout << "PASS: " << #a << " is false" << std::endl; \
    } \
} while(0)

void test_basic_coro() {
    bool executed = false;
    
    auto task = [&executed]() -> flowcoro::CoroTask {
        executed = true;
        co_return;
    }();
    
    // 协程在创建时不会自动执行完成
    EXPECT_FALSE(executed);
    
    // 运行协程
    task.resume();
    
    // 协程执行完成后应该标记为已完成
    EXPECT_TRUE(task.done());
}

// 测试带返回值的协程
void test_task_with_value() {
    flowcoro::Task<int> task = []() -> flowcoro::Task<int> {
        co_return 42;
    }();
    
    int result = task.get();
    EXPECT_EQ(result, 42);
}

// 模拟网络请求类，避免依赖libcurl
namespace flowcoro {

class MockHttpRequest : public INetworkRequest {
public:
    void request(const std::string& url, const std::function<void(const std::string&)>& callback) override {
        // 创建网络请求实例并发起请求
        std::thread([callback, url]() {
            // 模拟异步响应
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 执行回调
            try {
                callback("Mock response for: " + url);
            } catch (...) {
                // 捕获并处理可能的异常
                std::cerr << "Callback threw an exception" << std::endl;
            }
        }).detach();
    }
};

} // namespace flowcoro

// 测试异步网络请求
void test_network_request() {
    std::mutex mtx;
    std::condition_variable cv;
    bool completed = false;

    // 创建一个简单的网络请求测试
    auto task = [&]() -> flowcoro::CoroTask {
        std::string response = co_await flowcoro::CoroTask::execute_network_request<flowcoro::MockHttpRequest>("http://example.com");
        EXPECT_FALSE(response.empty());

        {
            std::lock_guard<std::mutex> lock(mtx);
            completed = true;
        }
        cv.notify_one();

        co_return;
    }();

    // 运行协程
    task.resume();

    // 等待协程完成
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool finished = cv.wait_for(lock, std::chrono::seconds(5), [&]{ return completed; });
        EXPECT_TRUE(finished);
    }

    EXPECT_TRUE(completed);
}

// 主函数
int main(int argc, char** argv) {
    std::cout << "Running FlowCoro tests..." << std::endl;
    
    test_basic_coro();
    test_task_with_value();
    // test_network_request();
    
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
