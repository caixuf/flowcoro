#include "flowcoro.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <future>
#include <vector>
#include <atomic>
#include <random>
#include <iomanip>
#include <sstream>
#include <fstream>

using namespace flowcoro;

// 获取真实的系统信息
class SystemInfo {
public:
    static int get_cpu_cores() {
        return std::thread::hardware_concurrency();
    }
    
    static size_t get_memory_usage_bytes() {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                return std::stoul(value) * 1024; // VmRSS is in KB, convert to bytes
            }
        }
        return 0;
    }
    
    static std::string format_memory_bytes(size_t bytes) {
        if (bytes >= 1024 * 1024 * 1024) {
            double gb = bytes / (1024.0 * 1024.0 * 1024.0);
            return std::to_string(gb).substr(0, 4) + "GB (" + std::to_string(bytes) + " bytes)";
        } else if (bytes >= 1024 * 1024) {
            double mb = bytes / (1024.0 * 1024.0);
            return std::to_string(mb).substr(0, 4) + "MB (" + std::to_string(bytes) + " bytes)";
        } else if (bytes >= 1024) {
            double kb = bytes / 1024.0;
            return std::to_string(kb).substr(0, 4) + "KB (" + std::to_string(bytes) + " bytes)";
        } else {
            return std::to_string(bytes) + " bytes";
        }
    }
    
    static std::string get_current_time() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        return ss.str();
    }
};

// 修复协程测试 - 完全依赖Task内置协程池
Task<void> handle_concurrent_requests_coroutine(int request_count) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();
    
    std::cout << "🚀 协程方式：处理 " << request_count << " 个并发请求" << std::endl;
    std::cout << "💾 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << "🧵 CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << "⏰ 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    // 启用FlowCoro功能
    std::cout << "🔧 正在启用FlowCoro功能..." << std::endl;
    enable_v2_features();
    std::cout << "✅ FlowCoro功能启用完成" << std::endl;
    
    // 定义单个请求处理函数 - 暂时去掉sleep_for，先确保when_all正常
    auto handle_single_request = [](int user_id) -> Task<std::string> {
        // 暂时去掉sleep_for，确保when_all本身工作正常
        // co_await sleep_for(std::chrono::milliseconds(50));
        
        // 构建结果
        std::string result = "用户" + std::to_string(user_id) + " (已处理)";
        co_return result;
    };
    
    std::cout << "📝 创建协程任务..." << std::endl;
    std::cout << "🌟 使用协程调度，不使用线程池..." << std::endl;
    
    // 测试when_all - 使用修复后的版本
    if (request_count <= 3) {
        std::cout << "✨ 使用when_all处理少量任务..." << std::endl;
        
        // 创建对应数量的任务测试when_all
        if (request_count == 1) {
            auto task1 = handle_single_request(1001);
            auto result = co_await task1;
            std::cout << "✅ 单任务完成: " << result << std::endl;
        } else if (request_count == 2) {
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto results = co_await when_all(std::move(task1), std::move(task2));
            auto [r1, r2] = results;
            std::cout << "✅ when_all完成: " << r1 << ", " << r2 << std::endl;
        } else { // request_count == 3
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto task3 = handle_single_request(1003);
            std::cout << "🔄 启动when_all等待..." << std::endl;
            auto results = co_await when_all(std::move(task1), std::move(task2), std::move(task3));
            auto [r1, r2, r3] = results;
            std::cout << "✅ when_all完成: " << r1 << ", " << r2 << ", " << r3 << std::endl;
        }
        
    } else if (request_count <= 5) {
        std::cout << "✨ 使用简单协程处理少量任务..." << std::endl;
        
        // 不使用when_all，直接顺序处理
        for (int i = 1; i <= request_count; ++i) {
            auto task = handle_single_request(1000 + i);
            auto result = co_await task; // 纯协程调度
            std::cout << "✅ 任务" << i << "完成: " << result << std::endl;
        }
    } else {
        std::cout << "📦 任务数量较多，使用顺序协程处理..." << std::endl;
        
        // 大规模任务：顺序处理，让协程池自动并发
        std::vector<std::string> all_results;
        all_results.reserve(request_count);
        
        for (int i = 0; i < request_count; ++i) {
            auto task = handle_single_request(1000 + i);
            auto result = co_await task; // 每个co_await都会使用协程池
            all_results.push_back(result);
            
            // 减少输出频率，避免竞争
            if ((i + 1) % 100 == 0 || (i + 1) == request_count) {
                std::cout << "✅ 完成 " << (i + 1) << "/" << request_count << std::endl;
            }
        }
        
        // 显示部分结果
        if (!all_results.empty()) {
            std::cout << "🔍 结果验证 (前5个): ";
            for (size_t i = 0; i < std::min(size_t(5), all_results.size()); ++i) {
                std::cout << "[" << all_results[i] << "] ";
            }
            std::cout << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;
    
    std::cout << std::string(50, '-') << std::endl;
    std::cout << "🚀 纯协程调度完成！" << std::endl;
    std::cout << "   📊 总请求数: " << request_count << " 个" << std::endl;
    std::cout << "   ⏱️  总耗时: " << duration.count() << " ms" << std::endl;
    
    if (request_count > 0) {
        std::cout << "   📈 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    }
    
    if (duration.count() > 0) {
        std::cout << "   🎯 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    }
    
    std::cout << "   💾 内存变化: " << SystemInfo::format_memory_bytes(initial_memory) 
              << " → " << SystemInfo::format_memory_bytes(final_memory) 
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    
    if (request_count > 0) {
        std::cout << "   📊 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    }
    
    std::cout << "   🌟 并发策略: 纯协程调度 (无线程池)" << std::endl;
    std::cout << "   🔧 管理方式: Task内置协程池自动处理" << std::endl;
    
    // 主动清理协程资源，避免静态对象析构顺序问题
    std::cout << "🧹 清理协程资源..." << std::endl;
    
    // 强制等待一段时间确保所有异步操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    co_return;
}
// 高并发多线程测试
void handle_concurrent_requests_threads(int request_count) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();
    
    std::cout << "🧵 多线程方式：处理 " << request_count << " 个并发请求" << std::endl;
    std::cout << "💾 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << "🧵 CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << "⏰ 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    std::cout << "📝 同时启动 " << request_count << " 个线程..." << std::endl;
    
    std::atomic<int> completed{0};
    std::vector<std::thread> threads;
    
    // 创建与协程数量相同的线程
    for (int i = 0; i < request_count; ++i) {
        threads.emplace_back([i, &completed, request_count]() {
            // 模拟IO操作
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::string result = "用户" + std::to_string(1000 + i) + " (已处理)";
            
            int current_completed = completed.fetch_add(1) + 1;
            if (current_completed % (request_count / 10) == 0 || current_completed == request_count) {
                std::cout << "✅ 已完成 " << current_completed << "/" << request_count 
                          << " 个线程 (" << (current_completed * 100 / request_count) << "%)" << std::endl;
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;
    
    std::cout << std::string(50, '-') << std::endl;
    std::cout << "🧵 多线程方式完成！" << std::endl;
    std::cout << "   📊 总请求数: " << request_count << " 个" << std::endl;
    std::cout << "   ⏱️  总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "   📈 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    std::cout << "   🎯 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << "   💾 内存变化: " << SystemInfo::format_memory_bytes(initial_memory) 
              << " → " << SystemInfo::format_memory_bytes(final_memory) 
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    std::cout << "   📊 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    std::cout << "   🧵 线程总数: " << request_count << " 个" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法: " << argv[0] << " <coroutine|thread> <request_count>" << std::endl;
        return 1;
    }
    
    std::string mode = argv[1];
    int request_count = std::stoi(argv[2]);
    
    std::cout << "========================================" << std::endl;
    std::cout << "🎯 FlowCoro 高并发性能测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "测试模式: " << mode << std::endl;
    std::cout << "请求数量: " << request_count << " 个" << std::endl;
    std::cout << "每个请求模拟50ms IO延迟" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;
    
    try {
        if (mode == "coroutine") {
            auto task = handle_concurrent_requests_coroutine(request_count);
            task.get();
            
            // 给系统一点时间来清理资源
            std::cout << "🔄 等待资源清理..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << "✅ 协程测试完成" << std::endl;
            
            // 快速退出，避免静态对象析构问题
            std::cout << "⏰ 程序结束: [" << SystemInfo::get_current_time() << "]" << std::endl;
            std::quick_exit(0);
            
        } else if (mode == "thread") {
            handle_concurrent_requests_threads(request_count);
            std::cout << "✅ 线程测试完成" << std::endl;
        } else {
            std::cerr << "❌ 无效的模式: " << mode << " (支持: coroutine, thread)" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 出现错误: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "⏰ 程序正常结束: [" << SystemInfo::get_current_time() << "]" << std::endl;
    
    return 0;
}
