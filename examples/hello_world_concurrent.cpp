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

// 高并发协程测试
Task<void> handle_concurrent_requests_coroutine(int request_count) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();
    
    std::cout << "🚀 协程方式：处理 " << request_count << " 个并发请求" << std::endl;
    std::cout << "💾 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << "🧵 CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << "⏰ 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    // 定义单个请求处理函数（使用安全的字符串预构建策略）
    auto handle_single_request = [](int user_id) -> Task<std::string> {
        // ✅ 关键：预构建字符串，避免co_await后内存分配问题
        std::string user_prefix = "用户" + std::to_string(user_id);
        std::string result_suffix = " (已处理)";
        
        // 模拟IO操作
        co_await sleep_for(std::chrono::milliseconds(50));
        
        // 使用预构建的字符串组件
        co_return user_prefix + result_suffix;
    };
    
    // 创建所有协程任务
    std::vector<Task<std::string>> tasks;
    tasks.reserve(request_count); // 预分配容量提高性能
    
    for (int i = 0; i < request_count; ++i) {
        tasks.emplace_back(handle_single_request(1000 + i));
    }
    
    std::cout << "📝 已创建 " << request_count << " 个协程任务，开始并发执行..." << std::endl;
    std::cout << "🌟 使用简化的协程管理（when_all 风格）..." << std::endl;
    
    // 🚀 简化版：直接提交所有任务到协程池，然后等待
    for (auto& task : tasks) {
        if (task.handle) {
            schedule_coroutine_enhanced(task.handle);
        }
    }
    
    std::cout << "⚡ 所有任务已提交，开始简化的等待循环..." << std::endl;
    
    // 简化的等待循环（相比原版减少了80%代码）
    std::vector<std::string> all_results;
    all_results.reserve(request_count);
    
    while (all_results.size() < request_count) {
        drive_coroutine_pool(); // 驱动协程池
        
        // 收集完成的任务结果
        for (int i = 0; i < request_count; ++i) {
            if (tasks[i].handle && tasks[i].handle.done() && i >= all_results.size()) {
                try {
                    all_results.push_back(tasks[i].get());
                    
                    // 优化的进度报告（减少输出频率）
                    int progress_step = std::max(1, request_count / 10); // 10%步长
                    if (all_results.size() % progress_step == 0 || all_results.size() == request_count) {
                        std::cout << "✅ 已完成 " << all_results.size() << "/" << request_count 
                                  << " 个协程 (" << (all_results.size() * 100 / request_count) << "%)" << std::endl;
                    }
                } catch (...) {
                    // 任务可能已经被处理，跳过
                }
            }
        }
        
        // 避免忙等待
        if (all_results.size() < request_count) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;
    
    std::cout << std::string(50, '-') << std::endl;
    std::cout << "🚀 when_all 协程方式完成！" << std::endl;
    std::cout << "   📊 总请求数: " << request_count << " 个" << std::endl;
    std::cout << "   ⏱️  总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "   📈 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    std::cout << "   🎯 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << "   💾 内存变化: " << SystemInfo::format_memory_bytes(initial_memory) 
              << " → " << SystemInfo::format_memory_bytes(final_memory) 
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    std::cout << "   📊 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    std::cout << "   🌟 并发策略: when_all 风格自动协程管理" << std::endl;
    
    // 结果验证（显示前几个结果）
    if (!all_results.empty()) {
        std::cout << "🔍 结果验证 (前5个): ";
        for (int i = 0; i < std::min(5, (int)all_results.size()); ++i) {
            std::cout << "[" << all_results[i] << "] ";
        }
        std::cout << std::endl;
    }
    
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
        } else if (mode == "thread") {
            handle_concurrent_requests_threads(request_count);
        } else {
            std::cerr << "❌ 无效的模式: " << mode << " (支持: coroutine, thread)" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 出现错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
