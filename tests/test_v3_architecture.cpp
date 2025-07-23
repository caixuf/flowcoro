#include "../include/flowcoro/v3_core.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <utility>

using namespace flowcoro::v3;

// 协程测试函数
struct TestTask {
    struct promise_type {
        TestTask get_return_object() {
            return TestTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
    
    std::coroutine_handle<promise_type> h;
    
    TestTask(std::coroutine_handle<promise_type> handle) : h(handle) {}
    ~TestTask() { if (h) h.destroy(); }
    
    TestTask(const TestTask&) = delete;
    TestTask& operator=(const TestTask&) = delete;
    
    TestTask(TestTask&& other) noexcept : h(std::exchange(other.h, {})) {}
    TestTask& operator=(TestTask&& other) noexcept {
        if (this != &other) {
            if (h) h.destroy();
            h = std::exchange(other.h, {});
        }
        return *this;
    }
};

// 简单的协程任务
TestTask simple_coroutine(int id, int work_amount) {
    std::cout << "🚀 协程 " << id << " 开始执行 (工作量: " << work_amount << ")" << std::endl;
    
    // 模拟一些工作
    int result = 0;
    for (int i = 0; i < work_amount; ++i) {
        result += i;
        if (i % 10000 == 0) {
            co_await std::suspend_always{}; // 让出执行权
        }
    }
    
    std::cout << "✅ 协程 " << id << " 完成，结果: " << result << std::endl;
    co_return;
}

// 内存使用测试
TestTask memory_intensive_coroutine(int id, size_t memory_size) {
    std::vector<char> buffer(memory_size, 'A' + (id % 26));
    
    std::cout << "💾 协程 " << id << " 分配了 " << memory_size << " 字节内存" << std::endl;
    
    // 模拟内存使用
    for (size_t i = 0; i < buffer.size(); i += 1024) {
        buffer[i] = static_cast<char>('A' + (i % 26));
        if (i % 10240 == 0) {
            co_await std::suspend_always{};
        }
    }
    
    std::cout << "📝 协程 " << id << " 完成内存操作" << std::endl;
    co_return;
}

// 工作窃取测试
TestTask cpu_intensive_coroutine(int id, int iterations) {
    volatile int dummy = 0;
    
    for (int i = 0; i < iterations; ++i) {
        // CPU 密集型工作
        for (int j = 0; j < 1000; ++j) {
            dummy += j * i;
        }
        
        if (i % 100 == 0) {
            co_await std::suspend_always{}; // 定期让出
        }
    }
    
    std::cout << "🔥 CPU 密集型协程 " << id << " 完成 (" << iterations << " 次迭代)" << std::endl;
    co_return;
}

void test_basic_functionality() {
    std::cout << "\n🧪 测试 1: 基本功能测试" << std::endl;
    std::cout << "=========================" << std::endl;
    
    // 初始化运行时
    FlowCoroRuntime::Config config;
    config.num_scheduler_threads = 4;
    config.initial_stack_pool_size = 50;
    FlowCoroRuntime::initialize(config);
    
    auto& runtime = FlowCoroRuntime::get_instance();
    
    // 创建一些简单协程
    std::vector<TestTask> tasks;
    for (int i = 0; i < 10; ++i) {
        tasks.push_back(simple_coroutine(i, 50000));
        runtime.schedule_coroutine(tasks.back().h);
    }
    
    // 等待执行
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    runtime.print_stats();
}

void test_memory_efficiency() {
    std::cout << "\n🧪 测试 2: 内存效率测试" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    auto initial_stats = runtime.get_stats();
    
    // 创建大量内存密集型协程
    std::vector<TestTask> memory_tasks;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> size_dist(1024, 8192);
    
    for (int i = 0; i < 50; ++i) {
        size_t memory_size = size_dist(gen);
        memory_tasks.push_back(memory_intensive_coroutine(i, memory_size));
        runtime.schedule_coroutine(memory_tasks.back().h);
    }
    
    // 等待执行
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto final_stats = runtime.get_stats();
    std::cout << "🔍 内存池利用率提升: " 
              << (final_stats.stack_pool_stats.utilization_rate * 100) << "%" << std::endl;
              
    runtime.print_stats();
}

void test_work_stealing() {
    std::cout << "\n🧪 测试 3: 工作窃取调度测试" << std::endl;
    std::cout << "=============================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    
    // 创建不同强度的 CPU 密集型任务
    std::vector<TestTask> cpu_tasks;
    
    // 几个长任务
    for (int i = 0; i < 3; ++i) {
        cpu_tasks.push_back(cpu_intensive_coroutine(i, 1000));
        runtime.schedule_coroutine(cpu_tasks.back().h, 1); // 高优先级
    }
    
    // 许多短任务
    for (int i = 3; i < 20; ++i) {
        cpu_tasks.push_back(cpu_intensive_coroutine(i, 100));
        runtime.schedule_coroutine(cpu_tasks.back().h, 0); // 普通优先级
    }
    
    auto start_time = std::chrono::steady_clock::now();
    
    // 等待执行
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "⏱️  工作窃取测试完成，耗时: " << duration.count() << " ms" << std::endl;
    runtime.print_stats();
}

void test_stack_pool_scaling() {
    std::cout << "\n🧪 测试 4: 栈池扩展测试" << std::endl;
    std::cout << "=========================" << std::endl;
    
    auto& runtime = FlowCoroRuntime::get_instance();
    auto initial_stats = runtime.get_stats();
    
    std::cout << "🔄 初始栈池大小: " << initial_stats.stack_pool_stats.total_stacks << std::endl;
    
    // 创建大量协程以触发栈池扩展
    std::vector<TestTask> scaling_tasks;
    for (int i = 0; i < 100; ++i) {
        scaling_tasks.push_back(simple_coroutine(i, 10000));
        runtime.schedule_coroutine(scaling_tasks.back().h);
    }
    
    // 等待扩展
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto final_stats = runtime.get_stats();
    std::cout << "📈 扩展后栈池大小: " << final_stats.stack_pool_stats.total_stacks << std::endl;
    std::cout << "📊 栈池峰值使用: " << final_stats.stack_pool_stats.peak_usage << std::endl;
    
    runtime.print_stats();
}

void benchmark_vs_threads() {
    std::cout << "\n🏁 性能对比: FlowCoro 3.0 vs 线程" << std::endl;
    std::cout << "====================================" << std::endl;
    
    const int num_tasks = 1000;
    const int work_per_task = 10000;
    
    // FlowCoro 3.0 测试
    auto start_coro = std::chrono::high_resolution_clock::now();
    
    auto& runtime = FlowCoroRuntime::get_instance();
    std::vector<TestTask> coro_tasks;
    
    for (int i = 0; i < num_tasks; ++i) {
        coro_tasks.push_back(simple_coroutine(i, work_per_task));
        runtime.schedule_coroutine(coro_tasks.back().h);
    }
    
    // 等待所有协程完成（简化实现）
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    auto end_coro = std::chrono::high_resolution_clock::now();
    auto coro_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_coro - start_coro);
    
    std::cout << "🚀 FlowCoro 3.0 (" << num_tasks << " 协程): " << coro_duration.count() << " ms" << std::endl;
    
    auto final_stats = runtime.get_stats();
    std::cout << "💾 峰值栈池使用: " << final_stats.stack_pool_stats.peak_usage << " / " 
              << final_stats.stack_pool_stats.total_stacks << std::endl;
    std::cout << "🎯 栈池利用率: " << std::fixed << std::setprecision(1)
              << (final_stats.stack_pool_stats.utilization_rate * 100) << "%" << std::endl;
}

int main() {
    std::cout << "🎯 FlowCoro 3.0 架构测试套件" << std::endl;
    std::cout << "============================" << std::endl;
    
    try {
        test_basic_functionality();
        test_memory_efficiency();
        test_work_stealing();
        test_stack_pool_scaling();
        benchmark_vs_threads();
        
        std::cout << "\n🎉 所有测试完成！" << std::endl;
        
        // 关闭运行时
        FlowCoroRuntime::shutdown();
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 测试异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
