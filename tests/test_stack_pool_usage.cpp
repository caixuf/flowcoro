#include "../include/flowcoro/v3_core.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <utility>

using namespace flowcoro::v3;

// 真实栈池使用的协程任务
struct StackAwareTask {
    struct promise_type {
        CoroutineStackPool::StackBlock* stack_block = nullptr;
        CoroutineHandleManager::ManagedHandle* managed_handle = nullptr;
        std::exception_ptr exception;
        
        StackAwareTask get_return_object() {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            
            // 从栈池获取栈空间
            auto& runtime = FlowCoroRuntime::get_instance();
            stack_block = runtime.get_stack_pool().acquire_stack();
            
            if (stack_block) {
                std::cout << "🔄 协程获取栈块 ID: " << stack_block->block_id << std::endl;
            }
            
            // 注册到句柄管理器
            managed_handle = runtime.get_handle_manager().register_handle(handle, "StackAwareTask");
            if (managed_handle) {
                managed_handle->stack_block = stack_block;
            }
            
            return StackAwareTask{handle};
        }
        
        std::suspend_always initial_suspend() { return {}; }
        
        std::suspend_always final_suspend() noexcept { 
            // 释放栈块回到池中
            if (stack_block) {
                auto& runtime = FlowCoroRuntime::get_instance();
                std::cout << "🔄 协程释放栈块 ID: " << stack_block->block_id << std::endl;
                runtime.get_stack_pool().release_stack(stack_block);
                stack_block = nullptr;
            }
            return {}; 
        }
        
        void return_void() {}
        void unhandled_exception() { exception = std::current_exception(); }
        
        ~promise_type() {
            // 确保栈块被释放
            if (stack_block) {
                auto& runtime = FlowCoroRuntime::get_instance();
                runtime.get_stack_pool().release_stack(stack_block);
            }
            
            // 从句柄管理器注销
            if (managed_handle) {
                auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
                auto& runtime = FlowCoroRuntime::get_instance();
                runtime.get_handle_manager().unregister_handle(handle);
            }
        }
    };
    
    std::coroutine_handle<promise_type> h;
    
    StackAwareTask(std::coroutine_handle<promise_type> handle) : h(handle) {}
    ~StackAwareTask() { if (h) h.destroy(); }
    
    StackAwareTask(const StackAwareTask&) = delete;
    StackAwareTask& operator=(const StackAwareTask&) = delete;
    
    StackAwareTask(StackAwareTask&& other) noexcept : h(std::exchange(other.h, {})) {}
    StackAwareTask& operator=(StackAwareTask&& other) noexcept {
        if (this != &other) {
            if (h) h.destroy();
            h = std::exchange(other.h, {});
        }
        return *this;
    }
};

// 深度递归协程：真实利用栈空间
StackAwareTask recursive_coroutine(int depth, int max_depth, int task_id) {
    // 在栈上分配一些数据来使用栈空间
    char stack_buffer[4096];  // 4KB 栈缓冲区
    volatile int* stack_data = (volatile int*)stack_buffer;
    
    // 填充栈数据
    for (int i = 0; i < 1024; ++i) {
        stack_data[i] = task_id * 1000 + depth * 10 + i;
    }
    
    std::cout << "📊 协程 " << task_id << " 深度 " << depth << " 使用栈空间: " 
              << sizeof(stack_buffer) << " 字节 (栈地址: " << (void*)stack_buffer << ")" << std::endl;
    
    if (depth < max_depth) {
        // 让出执行权
        co_await std::suspend_always{};
        
        // 递归调用更深层次
        auto subtask = recursive_coroutine(depth + 1, max_depth, task_id);
        FlowCoroRuntime::get_instance().schedule_coroutine(subtask.h);
        
        // 等待子任务（简化实现）
        co_await std::suspend_always{};
    }
    
    // 验证栈数据完整性
    bool data_intact = true;
    for (int i = 0; i < 1024; ++i) {
        if (stack_data[i] != task_id * 1000 + depth * 10 + i) {
            data_intact = false;
            break;
        }
    }
    
    std::cout << "✅ 协程 " << task_id << " 深度 " << depth 
              << " 栈数据完整性: " << (data_intact ? "OK" : "CORRUPTED") << std::endl;
    
    co_return;
}

// 长期运行的协程：测试栈池的复用
StackAwareTask long_running_coroutine(int task_id, int iterations) {
    char large_stack_buffer[8192];  // 8KB 栈缓冲区
    volatile char* data = (volatile char*)large_stack_buffer;
    
    for (int iter = 0; iter < iterations; ++iter) {
        // 填充栈数据
        for (int i = 0; i < 8192; ++i) {
            data[i] = (char)((task_id + iter + i) % 256);
        }
        
        std::cout << "🔄 长期协程 " << task_id << " 迭代 " << iter 
                  << " (栈地址: " << (void*)large_stack_buffer << ")" << std::endl;
        
        // 模拟一些计算工作
        volatile int sum = 0;
        for (int i = 0; i < 1000; ++i) {
            sum += data[i % 8192];
        }
        
        // 定期让出执行权，让其他协程有机会复用栈
        if (iter % 5 == 0) {
            co_await std::suspend_always{};
        }
    }
    
    std::cout << "🏁 长期协程 " << task_id << " 完成" << std::endl;
    co_return;
}

// 内存密集型协程：测试栈池的扩展
StackAwareTask memory_intensive_coroutine(int task_id, size_t memory_size) {
    // 动态分配栈内存（在栈上分配大数组）
    if (memory_size > 32 * 1024) {
        std::cout << "⚠️  协程 " << task_id << " 请求的内存过大，降低到 32KB" << std::endl;
        memory_size = 32 * 1024;
    }
    
    // 使用 alloca 在栈上分配（注意：这是示例，生产代码要谨慎使用）
    volatile char* stack_memory = (volatile char*)alloca(memory_size);
    
    std::cout << "💾 协程 " << task_id << " 分配栈内存: " << memory_size 
              << " 字节 (地址: " << (void*)stack_memory << ")" << std::endl;
    
    // 写入数据
    for (size_t i = 0; i < memory_size; ++i) {
        stack_memory[i] = (char)((task_id + i) % 256);
    }
    
    // 让出执行权
    co_await std::suspend_always{};
    
    // 验证数据
    bool data_valid = true;
    for (size_t i = 0; i < memory_size; ++i) {
        if (stack_memory[i] != (char)((task_id + i) % 256)) {
            data_valid = false;
            break;
        }
    }
    
    std::cout << "✅ 协程 " << task_id << " 内存验证: " 
              << (data_valid ? "通过" : "失败") << std::endl;
    
    co_return;
}

void test_stack_pool_basic_usage() {
    std::cout << "\n🧪 测试 1: 栈池基本使用" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    
    // 创建使用栈池的协程
    std::vector<StackAwareTask> tasks;
    for (int i = 0; i < 20; ++i) {
        tasks.push_back(recursive_coroutine(0, 3, i));
        runtime.schedule_coroutine(tasks.back().h);
    }
    
    // 等待执行
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto stats = runtime.get_stats();
    std::cout << "\n📊 栈池使用统计:" << std::endl;
    std::cout << "   总栈数: " << stats.stack_pool_stats.total_stacks << std::endl;
    std::cout << "   已分配: " << stats.stack_pool_stats.allocated_stacks << std::endl;
    std::cout << "   峰值使用: " << stats.stack_pool_stats.peak_usage << std::endl;
    std::cout << "   利用率: " << std::fixed << std::setprecision(1)
              << (stats.stack_pool_stats.utilization_rate * 100) << "%" << std::endl;
}

void test_stack_pool_reuse() {
    std::cout << "\n🧪 测试 2: 栈池复用测试" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    
    // 第一波协程
    {
        std::vector<StackAwareTask> wave1_tasks;
        for (int i = 0; i < 10; ++i) {
            wave1_tasks.push_back(long_running_coroutine(i, 5));
            runtime.schedule_coroutine(wave1_tasks.back().h);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        auto stats1 = runtime.get_stats();
        std::cout << "🌊 第一波峰值使用: " << stats1.stack_pool_stats.peak_usage << std::endl;
    }
    
    // 等待第一波完成并释放栈
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // 第二波协程（应该复用第一波的栈）
    {
        std::vector<StackAwareTask> wave2_tasks;
        for (int i = 100; i < 115; ++i) {
            wave2_tasks.push_back(long_running_coroutine(i, 8));
            runtime.schedule_coroutine(wave2_tasks.back().h);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        
        auto stats2 = runtime.get_stats();
        std::cout << "🌊 第二波峰值使用: " << stats2.stack_pool_stats.peak_usage << std::endl;
    }
    
    runtime.print_stats();
}

void test_stack_pool_expansion() {
    std::cout << "\n🧪 测试 3: 栈池扩展测试" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    auto initial_stats = runtime.get_stats();
    
    std::cout << "🔢 初始栈池大小: " << initial_stats.stack_pool_stats.total_stacks << std::endl;
    
    // 创建大量协程以触发扩展
    std::vector<StackAwareTask> expansion_tasks;
    for (int i = 0; i < 80; ++i) {
        expansion_tasks.push_back(memory_intensive_coroutine(i, 16384));  // 16KB
        runtime.schedule_coroutine(expansion_tasks.back().h);
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    auto final_stats = runtime.get_stats();
    std::cout << "📈 扩展后栈池大小: " << final_stats.stack_pool_stats.total_stacks << std::endl;
    std::cout << "📊 实际扩展数量: " 
              << (final_stats.stack_pool_stats.total_stacks - initial_stats.stack_pool_stats.total_stacks) 
              << std::endl;
    
    runtime.print_stats();
}

void test_stack_memory_safety() {
    std::cout << "\n🧪 测试 4: 栈内存安全测试" << std::endl;
    std::cout << "==========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    
    // 创建多个使用相同栈区域的协程
    std::vector<StackAwareTask> safety_tasks;
    for (int i = 0; i < 30; ++i) {
        safety_tasks.push_back(recursive_coroutine(0, 2, i + 1000));
        runtime.schedule_coroutine(safety_tasks.back().h);
        
        // 错开启动时间
        if (i % 5 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    std::cout << "🛡️  内存安全测试完成" << std::endl;
    runtime.print_stats();
}

int main() {
    std::cout << "🎯 FlowCoro 3.0 栈池真实使用测试" << std::endl;
    std::cout << "==================================" << std::endl;
    
    try {
        // 初始化运行时，使用较小的初始栈池来观察扩展
        FlowCoroRuntime::Config config;
        config.num_scheduler_threads = 4;
        config.initial_stack_pool_size = 20;  // 较小的初始大小
        config.enable_debug_mode = true;
        FlowCoroRuntime::initialize(config);
        
        auto& runtime = FlowCoroRuntime::get_instance();
        std::cout << "✅ 运行时初始化完成" << std::endl;
        runtime.print_stats();
        
        test_stack_pool_basic_usage();
        test_stack_pool_reuse();
        test_stack_pool_expansion();
        test_stack_memory_safety();
        
        std::cout << "\n🎉 所有栈池测试完成！" << std::endl;
        
        // 最终统计
        std::cout << "\n📈 最终统计报告:" << std::endl;
        runtime.print_stats();
        
        // 关闭运行时
        FlowCoroRuntime::shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
