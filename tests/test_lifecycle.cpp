#include <gtest/gtest.h>
#include "../include/flowcoro/enhanced_coroutine.h"
#include "../include/flowcoro/core.h"
#include <chrono>
#include <thread>
#include <atomic>

using namespace flowcoro;

class CoroutineLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 每个测试前的设置
    }
    
    void TearDown() override {
        // 每个测试后的清理
    }
};

// 测试安全协程句柄
TEST_F(CoroutineLifecycleTest, SafeCoroutineHandle) {
    auto simple_coro = []() -> enhanced_task<int> {
        co_return 42;
    };
    
    auto task = simple_coro();
    
    // 测试句柄有效性
    EXPECT_FALSE(task.is_ready());
    
    // 获取结果
    int result = task.get();
    EXPECT_EQ(result, 42);
    EXPECT_TRUE(task.is_ready());
    
    // 测试状态
    EXPECT_EQ(task.get_state(), detail::coroutine_state::completed);
}

// 测试取消机制
TEST_F(CoroutineLifecycleTest, CancellationMechanism) {
    std::atomic<bool> started{false};
    std::atomic<bool> cancelled_properly{false};
    
    auto cancellable_coro = [&](cancellation_token token) -> enhanced_task<int> {
        started = true;
        
        for (int i = 0; i < 100; ++i) {
            try {
                token.throw_if_cancelled();
                co_await sleep_for(std::chrono::milliseconds(10));
            } catch (const operation_cancelled_exception&) {
                cancelled_properly = true;
                throw;
            }
        }
        
        co_return 123;
    };
    
    cancellation_source cancel_source;
    auto token = cancel_source.get_token();
    auto task = cancellable_coro(token);
    
    // 在另一个线程中取消
    std::thread cancel_thread([&cancel_source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        cancel_source.cancel();
    });
    
    // 验证取消行为
    EXPECT_THROW({
        task.get();
    }, operation_cancelled_exception);
    
    cancel_thread.join();
    
    EXPECT_TRUE(started);
    EXPECT_TRUE(cancelled_properly);
    EXPECT_TRUE(task.is_cancelled());
    EXPECT_EQ(task.get_state(), detail::coroutine_state::cancelled);
}

// 测试状态转换
TEST_F(CoroutineLifecycleTest, StateTransitions) {
    detail::coroutine_state_manager state_manager;
    
    // 测试初始状态
    EXPECT_EQ(state_manager.get_state(), detail::coroutine_state::created);
    EXPECT_FALSE(state_manager.is_active());
    
    // 测试状态转换
    EXPECT_TRUE(state_manager.try_transition(
        detail::coroutine_state::created, 
        detail::coroutine_state::running
    ));
    EXPECT_TRUE(state_manager.is_active());
    
    // 测试无效转换
    EXPECT_FALSE(state_manager.try_transition(
        detail::coroutine_state::created, 
        detail::coroutine_state::suspended
    ));
    
    // 强制转换
    state_manager.force_transition(detail::coroutine_state::completed);
    EXPECT_EQ(state_manager.get_state(), detail::coroutine_state::completed);
    EXPECT_FALSE(state_manager.is_active());
}

// 测试生命周期计时
TEST_F(CoroutineLifecycleTest, LifetimeTiming) {
    auto timed_coro = []() -> enhanced_task<void> {
        co_await sleep_for(std::chrono::milliseconds(100));
        co_return;
    };
    
    auto task = timed_coro();
    
    auto start_time = std::chrono::steady_clock::now();
    task.get();
    auto end_time = std::chrono::steady_clock::now();
    
    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time
    );
    auto reported_duration = task.get_lifetime();
    
    // 允许一定的时间误差（±50ms）
    EXPECT_NEAR(actual_duration.count(), reported_duration.count(), 50);
    EXPECT_GE(reported_duration.count(), 90);  // 至少100ms左右
}

// 测试异常处理与状态
TEST_F(CoroutineLifecycleTest, ExceptionHandling) {
    auto exception_coro = []() -> enhanced_task<int> {
        co_await sleep_for(std::chrono::milliseconds(10));
        throw std::runtime_error("Test exception");
        co_return 42;  // 不会到达
    };
    
    auto task = exception_coro();
    
    EXPECT_THROW({
        task.get();
    }, std::runtime_error);
    
    // 检查异常后的状态
    EXPECT_EQ(task.get_state(), detail::coroutine_state::destroyed);
    EXPECT_FALSE(task.is_active());
}

// 测试多线程安全性
TEST_F(CoroutineLifecycleTest, ThreadSafety) {
    const int num_threads = 4;
    const int iterations_per_thread = 100;
    std::atomic<int> success_count{0};
    std::atomic<int> cancel_count{0};
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < iterations_per_thread; ++i) {
                auto coro = [](int value) -> enhanced_task<int> {
                    co_await sleep_for(std::chrono::milliseconds(1));
                    co_return value * 2;
                };
                
                auto task = coro(t * iterations_per_thread + i);
                
                // 随机取消一些任务
                if ((t * iterations_per_thread + i) % 7 == 0) {
                    task.cancel();
                    try {
                        task.get();
                    } catch (const operation_cancelled_exception&) {
                        cancel_count++;
                        continue;
                    }
                }
                
                try {
                    int result = task.get();
                    EXPECT_EQ(result, (t * iterations_per_thread + i) * 2);
                    success_count++;
                } catch (const operation_cancelled_exception&) {
                    cancel_count++;
                }
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    int total = num_threads * iterations_per_thread;
    EXPECT_EQ(success_count + cancel_count, total);
    EXPECT_GT(success_count, 0);
    EXPECT_GT(cancel_count, 0);
}

// 测试句柄安全销毁
TEST_F(CoroutineLifecycleTest, SafeDestruction) {
    // 创建很多协程句柄并快速销毁
    const int num_handles = 1000;
    
    for (int i = 0; i < num_handles; ++i) {
        auto coro = [i]() -> enhanced_task<int> {
            if (i % 2 == 0) {
                co_await sleep_for(std::chrono::milliseconds(1));
            }
            co_return i;
        };
        
        auto task = coro();
        
        // 一些任务立即销毁，一些完成后销毁
        if (i % 3 == 0) {
            // 立即销毁（测试未完成协程的清理）
            continue;
        } else {
            // 正常完成
            int result = task.get();
            EXPECT_EQ(result, i);
        }
    }
    
    // 如果到达这里没有崩溃，说明销毁机制工作正常
    SUCCEED();
}

// 测试取消回调
TEST_F(CoroutineLifecycleTest, CancellationCallbacks) {
    cancellation_source cancel_source;
    auto token = cancel_source.get_token();
    
    std::atomic<int> callback_count{0};
    
    // 注册多个回调
    for (int i = 0; i < 5; ++i) {
        token.register_callback([&callback_count, i]() {
            callback_count.fetch_add(1, std::memory_order_relaxed);
        });
    }
    
    EXPECT_EQ(callback_count.load(), 0);
    
    // 执行取消
    cancel_source.cancel();
    
    // 等待回调执行完成
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_EQ(callback_count.load(), 5);
    EXPECT_TRUE(token.is_cancelled());
}

// 性能基准测试
TEST_F(CoroutineLifecycleTest, PerformanceBenchmark) {
    const int num_iterations = 10000;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_iterations; ++i) {
        auto simple_coro = [i]() -> enhanced_task<int> {
            co_return i;
        };
        
        auto task = simple_coro();
        int result = task.get();
        EXPECT_EQ(result, i);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time
    );
    
    double avg_time_per_coro = double(duration.count()) / num_iterations;
    
    // 输出性能指标（不是严格的断言，用于性能监控）
    std::cout << "平均每个协程耗时: " << avg_time_per_coro << " 微秒\n";
    std::cout << "总耗时: " << duration.count() << " 微秒\n";
    
    // 确保性能在合理范围内（这个值可能需要根据实际环境调整）
    EXPECT_LT(avg_time_per_coro, 1000);  // 每个协程不超过1毫秒
}

// 内存泄漏检测测试
TEST_F(CoroutineLifecycleTest, MemoryLeakTest) {
    // 这个测试主要依赖外部工具（如Valgrind）检测内存泄漏
    // 这里我们创建和销毁大量对象来触发潜在的泄漏
    
    const int num_cycles = 100;
    
    for (int cycle = 0; cycle < num_cycles; ++cycle) {
        std::vector<enhanced_task<int>> tasks;
        
        for (int i = 0; i < 50; ++i) {
            auto coro = [i]() -> enhanced_task<int> {
                if (i % 10 == 0) {
                    throw std::runtime_error("Test exception");
                }
                co_return i;
            };
            
            tasks.push_back(coro());
        }
        
        // 处理所有任务
        for (auto& task : tasks) {
            try {
                task.get();
            } catch (const std::exception&) {
                // 忽略异常
            }
        }
        
        // tasks向量销毁时会清理所有协程
    }
    
    SUCCEED();  // 如果没有内存问题，测试通过
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
