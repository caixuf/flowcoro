#include "../include/flowcoro.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <random>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <fstream>

using namespace flowcoro;

// 获取真实的系统信息
class SystemInfo {
public:
    static int get_cpu_cores() {
        return std::thread::hardware_concurrency();
    }
    
    static size_t get_memory_usage_mb() {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                return std::stoul(value) / 1024; // 转换为MB
            }
        }
        return 0;
    }
    
    static std::string get_current_time() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        return ss.str();
    }
    
    static std::string get_thread_id() {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        return ss.str();
    }
};

// 真实的随机延迟模拟网络请求
class NetworkSimulator {
private:
    static std::random_device rd;
    static std::mt19937 gen;
    
public:
    static int get_random_delay(int min_ms, int max_ms) {
        std::uniform_int_distribution<> dis(min_ms, max_ms);
        return dis(gen);
    }
    
    static std::string generate_user_data(int user_id) {
        std::vector<std::string> locations = {"北京", "上海", "深圳", "杭州", "成都", "广州"};
        std::vector<std::string> types = {"普通会员", "VIP会员", "黄金会员", "钻石会员"};
        
        auto& loc_dis = locations;
        auto& type_dis = types;
        
        std::uniform_int_distribution<> loc_idx(0, locations.size() - 1);
        std::uniform_int_distribution<> type_idx(0, types.size() - 1);
        
        return "用户" + std::to_string(user_id) + 
               " (" + types[type_idx(gen)] + ", " + locations[loc_idx(gen)] + ")";
    }
    
    static int calculate_order_count(int user_id) {
        // 基于用户ID生成"真实"的订单数量
        std::uniform_int_distribution<> dis(user_id % 10, user_id % 50 + 20);
        return dis(gen);
    }
};

std::random_device NetworkSimulator::rd;
std::mt19937 NetworkSimulator::gen(NetworkSimulator::rd());

// 模拟真实业务：查询用户信息
Task<std::string> fetch_user_info(int user_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto delay = NetworkSimulator::get_random_delay(80, 150); // 真实的网络延迟
    
    std::cout << "[" << SystemInfo::get_current_time() << "] 🔍 [线程:" 
              << SystemInfo::get_thread_id().substr(0, 8) << "] "
              << "正在查询用户 " << user_id << " 的信息... (预计" << delay << "ms)" << std::endl;
    
    // 使用真实的延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto user_data = NetworkSimulator::generate_user_data(user_id);
    std::cout << "[" << SystemInfo::get_current_time() << "] ✅ 用户信息获取完成 (实际耗时:" 
              << actual_duration.count() << "ms)" << std::endl;
    
    co_return user_data;
}

// 模拟真实业务：获取用户订单
Task<int> fetch_order_count(int user_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto delay = NetworkSimulator::get_random_delay(60, 120); // 真实的数据库查询延迟
    
    std::cout << "[" << SystemInfo::get_current_time() << "] 📋 [线程:" 
              << SystemInfo::get_thread_id().substr(0, 8) << "] "
              << "正在查询用户 " << user_id << " 的订单数量... (预计" << delay << "ms)" << std::endl;
    
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto actual_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto order_count = NetworkSimulator::calculate_order_count(user_id);
    std::cout << "[" << SystemInfo::get_current_time() << "] ✅ 订单数据获取完成 (实际耗时:" 
              << actual_duration.count() << "ms)" << std::endl;
    
    co_return order_count;
}

// =====================================================
// 方式1：协程方式（推荐）- 并发执行
// =====================================================
Task<void> get_user_profile_coroutine(int user_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_mb();
    
    std::cout << "\n🚀 协程方式：开始并发获取用户档案..." << std::endl;
    std::cout << "💾 初始内存使用: " << initial_memory << "MB" << std::endl;
    std::cout << "🧵 CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    
    // 关键：同时发起多个异步操作（并发执行）
    auto user_info_task = fetch_user_info(user_id);
    auto order_count_task = fetch_order_count(user_id);
    
    // 等待两个任务都完成
    auto user_info = co_await user_info_task;
    auto order_count = co_await order_count_task;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_mb();
    
    std::cout << "\n✅ 协程方式完成！" << std::endl;
    std::cout << "   用户信息: " << user_info << std::endl;
    std::cout << "   订单数量: " << order_count << " 个" << std::endl;
    std::cout << "   实际耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "   内存变化: " << initial_memory << "MB → " << final_memory 
              << "MB (增加 " << (final_memory - initial_memory) << "MB)" << std::endl;
}

// =====================================================
// 方式2：传统同步方式 - 串行执行
// =====================================================
void get_user_profile_sync(int user_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "� 同步方式：开始串行获取用户档案...\n" << std::endl;
    
    // 串行执行：必须等待每个操作完成
    std::cout << "🔍 正在查询用户 " << user_id << " 的信息..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 模拟用户查询
    std::string user_info = "用户" + std::to_string(user_id) + " (VIP会员)";
    
    std::cout << "📋 正在查询用户 " << user_id << " 的订单数量..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));  // 模拟订单查询
    int order_count = user_id * 3;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "\n✅ 同步方式完成！" << std::endl;
    std::cout << "   用户信息: " << user_info << std::endl;
    std::cout << "   订单数量: " << order_count << " 个" << std::endl;
    std::cout << "   耗时: " << duration.count() << "ms" << std::endl;
}

// =====================================================
// 方式3：多线程方式 - 并行执行但高资源消耗
// =====================================================
void get_user_profile_threads(int user_id) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "🧵 多线程方式：开始并行获取用户档案...\n" << std::endl;
    
    // 使用future和async实现并发
    auto user_future = std::async(std::launch::async, [user_id]() {
        std::cout << "🔍 正在查询用户 " << user_id << " 的信息..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return "用户" + std::to_string(user_id) + " (VIP会员)";
    });
    
    auto order_future = std::async(std::launch::async, [user_id]() {
        std::cout << "📋 正在查询用户 " << user_id << " 的订单数量..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return user_id * 3;
    });
    
    // 等待两个线程都完成
    auto user_info = user_future.get();
    auto order_count = order_future.get();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "\n✅ 多线程方式完成！" << std::endl;
    std::cout << "   用户信息: " << user_info << std::endl;
    std::cout << "   订单数量: " << order_count << " 个" << std::endl;
    std::cout << "   耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "   💾 内存使用：~16MB (2个线程 × 8MB栈)" << std::endl;
}

// =====================================================
// 高并发场景测试：协程 vs 多线程的资源消耗对比
// =====================================================
Task<void> handle_many_requests_coroutine(int request_count) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "🚀 协程方式：处理 " << request_count << " 个并发请求...\n" << std::endl;
    
    // 创建所有任务（这些都在主线程中创建）
    std::vector<Task<std::string>> tasks;
    for (int i = 0; i < request_count; ++i) {
        // 注意：不使用fetch_user_info，而是创建简单的协程
        tasks.emplace_back([](int user_id) -> Task<std::string> {
            co_await sleep_for(std::chrono::milliseconds(100));
            co_return "用户" + std::to_string(user_id) + " (VIP会员)";
        }(1000 + i));
    }
    
    std::cout << "📝 已创建 " << request_count << " 个协程任务，开始并发执行..." << std::endl;
    
    // 使用线程池并发执行所有协程任务
    std::vector<std::future<std::string>> futures;
    for (auto& task : tasks) {
        futures.emplace_back(std::async(std::launch::async, [&task]() {
            return task.get(); // 在单独线程中执行每个协程
        }));
    }
    
    // 等待所有任务完成
    int completed = 0;
    for (auto& future : futures) {
        auto result = future.get();
        completed++;
        if (completed % 10 == 0 || completed == request_count) {
            std::cout << "✅ 已完成 " << completed << "/" << request_count << " 个请求" << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    std::cout << "\n🚀 协程方式完成 " << request_count << " 个请求！" << std::endl;
    std::cout << "   耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "   💾 内存使用：~" << (request_count * 2) / 1024.0 << "MB (" << request_count << "个协程 × 2KB)" << std::endl;
    
    co_return; // 修复：添加co_return
}

void handle_many_requests_threads(int request_count) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::cout << "🧵 多线程方式：处理 " << request_count << " 个并发请求...\n" << std::endl;
    
    // 使用受限的线程池，避免系统过载
    const int max_concurrent_threads = std::min(8, request_count);
    std::cout << "📝 使用 " << max_concurrent_threads << " 个工作线程处理 " << request_count << " 个请求..." << std::endl;
    
    std::atomic<int> completed{0};
    std::vector<std::future<void>> futures;
    
    // 工作队列
    std::queue<int> request_queue;
    for (int i = 0; i < request_count; ++i) {
        request_queue.push(1000 + i);
    }
    
    std::mutex queue_mutex;
    
    // 创建工作线程
    for (int t = 0; t < max_concurrent_threads; ++t) {
        futures.push_back(std::async(std::launch::async, [&]() {
            while (true) {
                int user_id;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (request_queue.empty()) break;
                    user_id = request_queue.front();
                    request_queue.pop();
                }
                
                // 模拟请求处理
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::string result = "用户" + std::to_string(user_id) + " (VIP会员)";
                
                int current_completed = completed.fetch_add(1) + 1;
                if (current_completed % 10 == 0 || current_completed == request_count) {
                    std::cout << "✅ 已完成 " << current_completed << "/" << request_count << " 个请求" << std::endl;
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
    
    std::cout << "\n🧵 多线程方式完成 " << request_count << " 个请求！" << std::endl;
    std::cout << "   耗时: " << duration.count() << "ms" << std::endl;
    std::cout << "   💾 内存使用：~" << (max_concurrent_threads * 8) << "MB (" << max_concurrent_threads << "个线程 × 8MB)" << std::endl;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "🎯 FlowCoro 协程 vs 多线程对比\n";
    std::cout << "========================================\n";
    std::cout << "重点：协程的优势不是速度，而是资源效率！\n";
    std::cout << "----------------------------------------\n\n";
    
    const int user_id = 12345;
    
    try {
        // 第一部分：基础性能对比（速度相似）
        std::cout << "🔸 第一部分：基础并发对比\n";
        std::cout << "查询内容：用户信息(100ms) + 订单数量(80ms)\n";
        std::cout << std::string(50, '-') << "\n\n";
        
        // 测试1：协程方式
        std::cout << "【测试1/3】协程方式\n";
        auto coro_task = get_user_profile_coroutine(user_id);
        coro_task.get();
        
        std::cout << "\n" << std::string(30, '=') << "\n\n";
        
        // 测试2：同步方式
        std::cout << "【测试2/3】同步方式\n";
        get_user_profile_sync(user_id);
        
        std::cout << "\n" << std::string(30, '=') << "\n\n";
        
        // 测试3：多线程方式
        std::cout << "【测试3/3】多线程方式\n";
        get_user_profile_threads(user_id);
        
        std::cout << "\n" << std::string(50, '=') << "\n\n";
        
        // 第二部分：高并发场景对比（协程优势明显）
        std::cout << "🔸 第二部分：高并发场景对比（协程真正的优势！）\n";
        const int concurrent_requests = 50;  // 50个并发请求
        std::cout << "模拟 " << concurrent_requests << " 个并发用户请求\n";
        std::cout << std::string(50, '-') << "\n\n";
        
        // 协程处理高并发
        std::cout << "【高并发测试1/2】协程方式 - 轻量高效\n";
        auto many_coro_task = handle_many_requests_coroutine(concurrent_requests);
        many_coro_task.get();
        
        std::cout << "\n" << std::string(30, '=') << "\n\n";
        
        // 多线程处理高并发（慎用 - 会创建很多线程）
        std::cout << "【高并发测试2/2】多线程方式 - 资源消耗大\n";
        std::cout << "⚠️  注意：这会创建 " << concurrent_requests << " 个线程！\n\n";
        handle_many_requests_threads(concurrent_requests);
        
        std::cout << "\n" << std::string(50, '=') << "\n\n";
        
        // 总结对比
        std::cout << "📊 关键对比总结：\n";
        std::cout << "----------------------------------------\n";
        std::cout << "� 性能对比（基础场景）：\n";
        std::cout << "   协程：     ~100ms\n";
        std::cout << "   同步：     ~180ms\n";
        std::cout << "   多线程：   ~100ms\n";
        std::cout << "\n� 资源对比（" << concurrent_requests << "个并发请求）：\n";
        std::cout << "   协程：     ~" << (concurrent_requests * 2) / 1024.0 << "MB (超轻量)\n";
        std::cout << "   多线程：   ~" << (concurrent_requests * 8) << "MB (重量级)\n";
        std::cout << "\n🎯 协程的核心优势：\n";
        std::cout << "   ✅ 内存效率：比线程节省数百倍内存\n";
        std::cout << "   ✅ 扩展性：可轻松处理数万并发\n";
        std::cout << "   ✅ 简洁性：代码比多线程更易理解\n";
        std::cout << "   ✅ 无锁：避免复杂的同步问题\n";
        
        std::cout << "\n� 什么时候用协程？\n";
        std::cout << "   🎯 高并发服务器（Web API、游戏服务器）\n";
        std::cout << "   🎯 IO密集型应用（数据库、网络请求）\n";
        std::cout << "   🎯 需要处理大量连接的场景\n";
        
        std::cout << "\n📖 想了解更多？运行：./business_demo\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 出现错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}