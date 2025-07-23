#include "../include/flowcoro.hpp"
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
    
    // 创建所有协程任务
    std::vector<Task<std::string>> tasks;
    for (int i = 0; i < request_count; ++i) {
        tasks.emplace_back([](int user_id) -> Task<std::string> {
            // 模拟IO操作
            co_await sleep_for(std::chrono::milliseconds(50));
            co_return "用户" + std::to_string(user_id) + " (已处理)";
        }(1000 + i));
    }
    
    std::cout << "📝 已创建 " << request_count << " 个协程任务，开始并发执行..." << std::endl;
    
    // 并发执行所有任务
    std::vector<std::future<std::string>> futures;
    for (auto& task : tasks) {
        futures.emplace_back(std::async(std::launch::async, [&task]() {
            return task.get();
        }));
    }
    
    // 等待所有任务完成并显示进度
    int completed = 0;
    for (auto& future : futures) {
        auto result = future.get();
        completed++;
        if (completed % (request_count / 10) == 0 || completed == request_count) {
            std::cout << "✅ 已完成 " << completed << "/" << request_count 
                      << " 个请求 (" << (completed * 100 / request_count) << "%)" << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;
    
    std::cout << std::string(50, '-') << std::endl;
    std::cout << "🚀 协程方式完成！" << std::endl;
    std::cout << "   📊 总请求数: " << request_count << " 个" << std::endl;
    std::cout << "   ⏱️  总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << "   📈 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    std::cout << "   🎯 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << "   💾 内存变化: " << SystemInfo::format_memory_bytes(initial_memory) 
              << " → " << SystemInfo::format_memory_bytes(final_memory) 
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    std::cout << "   📊 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    
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
    
    // 使用受限的线程池
    const int max_concurrent_threads = std::min(16, request_count);
    std::cout << "📝 使用 " << max_concurrent_threads << " 个工作线程处理 " 
              << request_count << " 个请求..." << std::endl;
    
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    std::atomic<int> request_counter{0};
    
    // 创建工作线程
    for (int t = 0; t < max_concurrent_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                int current_request = request_counter.fetch_add(1);
                if (current_request >= request_count) break;
                
                // 模拟IO操作
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::string result = "用户" + std::to_string(1000 + current_request) + " (已处理)";
                
                int current_completed = completed.fetch_add(1) + 1;
                if (current_completed % (request_count / 10) == 0 || current_completed == request_count) {
                    std::cout << "✅ 已完成 " << current_completed << "/" << request_count 
                              << " 个请求 (" << (current_completed * 100 / request_count) << "%)" << std::endl;
                }
            }
        }));
    }
    
    // 等待所有工作线程完成
    for (auto& future : futures) {
        future.get();
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
    std::cout << "   🧵 工作线程数: " << max_concurrent_threads << " 个" << std::endl;
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
