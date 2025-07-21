/**
 * @file test_lifecycle_basic.cpp
 * @brief FlowCoro协程生命周期管理基础单元测试
 * @author FlowCoro Team
 * @date 2025-07-21
 * 
 * 测试协程生命周期管理的核心功能：
 * 1. 安全句柄管理
 * 2. 状态管理器  
 * 3. 取消机制
 * 4. 增强任务
 */

#include <gtest/gtest.h>
#include "../include/flowcoro/lifecycle.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace flowcoro::lifecycle;
using namespace std::chrono_literals;

class LifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 重置统计信息
        auto& manager = coroutine_lifecycle_manager::get();
        // 注意：这里只是简单的测试，实际使用中应该有更好的重置机制
    }
    
    void TearDown() override {
        // 清理资源
        GlobalThreadPool::shutdown();
    }
};

// 测试1: 安全协程句柄基础功能
TEST_F(LifecycleTest, SafeCoroutineHandleBasics) {
    // 测试默认构造
    safe_coroutine_handle handle1;
    EXPECT_FALSE(handle1.valid());
    EXPECT_TRUE(handle1.done());
    
    // 测试移动构造
    safe_coroutine_handle handle2;
    auto moved_handle = std::move(handle2);
    EXPECT_FALSE(handle2.valid());  // 移动后应该无效
    
    // 测试析构安全性
    {
        safe_coroutine_handle temp_handle;
        // 应该安全析构，不会崩溃
    }
}

// 测试2: 协程状态管理器
TEST_F(LifecycleTest, CoroutineStateManager) {
    coroutine_state_manager manager;
    
    // 初始状态
    EXPECT_EQ(manager.get_state(), coroutine_state::created);
    EXPECT_TRUE(manager.is_active());
    EXPECT_FALSE(manager.is_terminal());
    
    // 状态转换
    EXPECT_TRUE(manager.try_transition(coroutine_state::created, coroutine_state::running));
    EXPECT_EQ(manager.get_state(), coroutine_state::running);
    
    // 无效转换
    EXPECT_FALSE(manager.try_transition(coroutine_state::created, coroutine_state::suspended));
    EXPECT_EQ(manager.get_state(), coroutine_state::running);
    
    // 强制转换到完成状态
    manager.force_transition(coroutine_state::completed);
    EXPECT_EQ(manager.get_state(), coroutine_state::completed);
    EXPECT_FALSE(manager.is_active());
    EXPECT_TRUE(manager.is_terminal());
}

// 测试3: 取消机制基础功能
TEST_F(LifecycleTest, CancellationBasics) {
    // 测试默认令牌（永不取消）
    cancellation_token default_token;
    EXPECT_FALSE(default_token.is_cancelled());
    EXPECT_FALSE(default_token.is_valid());
    
    // 测试取消源和令牌
    cancellation_source source;
    auto token = source.get_token();
    
    EXPECT_FALSE(source.is_cancelled());
    EXPECT_FALSE(token.is_cancelled());
    EXPECT_TRUE(token.is_valid());
    
    // 执行取消
    source.cancel();
    
    EXPECT_TRUE(source.is_cancelled());
    EXPECT_TRUE(token.is_cancelled());
    
    // 测试取消异常
    EXPECT_THROW(token.throw_if_cancelled(), operation_cancelled_exception);
}

// 测试4: 取消回调机制
TEST_F(LifecycleTest, CancellationCallbacks) {
    cancellation_source source;
    auto token = source.get_token();
    
    std::atomic<bool> callback_called{false};
    std::atomic<int> callback_count{0};
    
    // 注册回调
    auto registration1 = token.register_callback([&callback_called]() {
        callback_called.store(true);
    });
    
    auto registration2 = token.register_callback([&callback_count]() {
        callback_count.fetch_add(1);
    });
    
    EXPECT_FALSE(callback_called.load());
    EXPECT_EQ(callback_count.load(), 0);
    
    // 触发取消
    source.cancel();
    
    // 等待回调执行
    std::this_thread::sleep_for(10ms);
    
    EXPECT_TRUE(callback_called.load());
    EXPECT_EQ(callback_count.load(), 1);
}

// 测试5: 组合取消令牌
TEST_F(LifecycleTest, CombinedCancellationToken) {
    cancellation_source source1, source2, source3;
    
    auto combined = combine_tokens(
        source1.get_token(),
        source2.get_token(), 
        source3.get_token()
    );
    
    auto combined_token = combined.get_token();
    EXPECT_FALSE(combined_token.is_cancelled());
    
    // 取消第二个源
    source2.cancel();
    
    // 组合令牌应该被取消
    EXPECT_TRUE(combined_token.is_cancelled());
}

// 测试6: 增强任务基础功能
enhanced_task<int> simple_task(int value) {
    co_return value * 2;
}

enhanced_task<void> void_task() {
    co_return;
}

TEST_F(LifecycleTest, EnhancedTaskBasics) {
    // 测试有返回值的任务
    auto task = simple_task(21);
    
    EXPECT_FALSE(task.is_ready());
    EXPECT_EQ(task.get_state(), coroutine_state::created);
    
    auto result = task.get();
    EXPECT_EQ(result, 42);
    
    // 测试无返回值的任务
    auto void_task_instance = void_task();
    EXPECT_NO_THROW(void_task_instance.get());
}

// 测试7: 任务取消功能
enhanced_task<int> cancellable_task(cancellation_token token) {
    for (int i = 0; i < 100; ++i) {
        token.throw_if_cancelled();
        std::this_thread::sleep_for(10ms);
    }
    co_return 999;
}

TEST_F(LifecycleTest, TaskCancellation) {
    cancellation_source source;
    auto token = source.get_token();
    
    auto task = cancellable_task(token);
    task.set_cancellation_token(token);
    
    // 在另一个线程中取消任务
    std::thread canceller([&source]() {
        std::this_thread::sleep_for(50ms);
        source.cancel();
    });
    
    // 任务应该被取消并抛出异常
    EXPECT_THROW(task.get(), operation_cancelled_exception);
    
    canceller.join();
}

// 测试8: 生命周期统计
TEST_F(LifecycleTest, LifecycleStatistics) {
    auto& manager = coroutine_lifecycle_manager::get();
    auto initial_stats = manager.get_stats();
    
    // 模拟协程生命周期事件
    manager.on_coroutine_created();
    manager.on_coroutine_created(); 
    manager.on_coroutine_created();
    
    auto after_create_stats = manager.get_stats();
    EXPECT_EQ(after_create_stats.active_coroutines, 
              initial_stats.active_coroutines + 3);
    EXPECT_EQ(after_create_stats.total_created, 
              initial_stats.total_created + 3);
    
    // 完成一些协程
    manager.on_coroutine_completed();
    manager.on_coroutine_cancelled();
    manager.on_coroutine_failed();
    
    auto final_stats = manager.get_stats();
    EXPECT_EQ(final_stats.active_coroutines, initial_stats.active_coroutines);
    EXPECT_EQ(final_stats.completed_coroutines, 
              initial_stats.completed_coroutines + 1);
    EXPECT_EQ(final_stats.cancelled_coroutines, 
              initial_stats.cancelled_coroutines + 1);
    EXPECT_EQ(final_stats.failed_coroutines, 
              initial_stats.failed_coroutines + 1);
}

// 测试9: 协程守护
TEST_F(LifecycleTest, CoroutineGuard) {
    auto& manager = coroutine_lifecycle_manager::get();
    auto initial_stats = manager.get_stats();
    
    {
        coroutine_guard guard;
        auto stats = manager.get_stats();
        EXPECT_EQ(stats.active_coroutines, initial_stats.active_coroutines + 1);
        EXPECT_EQ(stats.total_created, initial_stats.total_created + 1);
    }
    
    // 守护析构后应该减少活跃协程数
    auto final_stats = manager.get_stats();
    EXPECT_EQ(final_stats.active_coroutines, initial_stats.active_coroutines);
    EXPECT_EQ(final_stats.completed_coroutines, initial_stats.completed_coroutines + 1);
}

// 测试10: 内存安全和资源清理
TEST_F(LifecycleTest, MemorySafety) {
    // 创建大量句柄和对象，确保没有内存泄漏
    std::vector<safe_coroutine_handle> handles;
    std::vector<coroutine_state_manager> managers;
    std::vector<cancellation_source> sources;
    
    for (int i = 0; i < 1000; ++i) {
        handles.emplace_back();
        managers.emplace_back();
        sources.emplace_back();
    }
    
    // 清空容器，应该安全析构所有对象
    handles.clear();
    managers.clear(); 
    sources.clear();
    
    // 如果到达这里没有崩溃，说明内存管理是安全的
    SUCCEED();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
