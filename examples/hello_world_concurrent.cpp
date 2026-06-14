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
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cjson/cJSON.h>

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
Task<void> handle_concurrent_requests_coroutine(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    std::cout << " 协程方式：处理 " << request_count << " 个并发请求" << std::endl;
    std::cout << " 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << " CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << " 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    // FlowCoro功能现在默认启用，无需手动调用
    std::cout << " FlowCoro功能已启用" << std::endl;

    // 定义单个请求处理函数
    // 注意：此处不加 sleep_for，专注测试协程纯调度吞吐量（无IO等待的任务切换效率）。
    // 如需模拟IO并发场景（体现协程叠加等待时延的能力），可取消下方注释。
    //
    // [Perf优化] 原来返回 Task<std::string>，每次任务都会做 SSO 溢出堆分配+拷贝。
    // callgrind 显示 std::string::_M_mutate 占 11.13% Ir、malloc_consolidate 占 2.47%。
    // 改为 Task<int> 返回 user_id，消除热路径上的动态内存分配。
    auto handle_single_request = [](int user_id) -> Task<int> {
        // co_await sleep_for(std::chrono::milliseconds(50)); // IO模拟（按需启用）
        co_return user_id; // 直接返回整型，零堆分配
    };

    std::cout << " 创建协程任务..." << std::endl;
    std::cout << " 使用协程调度，不使用线程池..." << std::endl;

    // 测试when_all - 使用修复后的版本
    if (request_count <= 3) {
        std::cout << " 使用when_all处理少量任务..." << std::endl;

        // 创建对应数量的任务测试when_all
        if (request_count == 1) {
            auto task1 = handle_single_request(1001);
            auto result = co_await task1;
            std::cout << " 单任务完成: " << result << std::endl;
        } else if (request_count == 2) {
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto results = co_await when_all(std::move(task1), std::move(task2));
            auto [r1, r2] = results;
            std::cout << " when_all完成: " << r1 << ", " << r2 << std::endl;
        } else { // request_count == 3
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto task3 = handle_single_request(1003);
            std::cout << " 启动when_all等待..." << std::endl;
            auto results = co_await when_all(std::move(task1), std::move(task2), std::move(task3));
            auto [r1, r2, r3] = results;
            std::cout << " when_all完成: " << r1 << ", " << r2 << ", " << r3 << std::endl;
        }

    } else if (request_count <= 5) {
        std::cout << " 使用简单协程处理少量任务..." << std::endl;

        // 不使用when_all，直接顺序处理
        for (int i = 1; i <= request_count; ++i) {
            auto task = handle_single_request(1000 + i);
            auto result = co_await task; // 纯协程调度
            std::cout << " 任务" << i << "完成: " << result << std::endl;
        }
    } else {
        std::cout << " 任务数量较多，使用真正并发协程处理..." << std::endl;

        // 大规模任务：先创建所有任务（利用Task同步启动特性）
        // [Perf优化] Task<int> 替代 Task<std::string>，消除热路径堆分配
        std::vector<Task<int>> tasks;
        tasks.reserve(request_count);

        std::cout << " 创建所有协程任务 (它们将同步开始执行直到挂起)..." << std::endl;
        
        // 同时创建所有任务 - 它们会同步开始执行直到挂起点
        for (int i = 0; i < request_count; ++i) {
            tasks.emplace_back(handle_single_request(1000 + i));
        }

        std::cout << " 所有任务已创建，等待完成..." << std::endl;

        // 优化：减少内存分配，移除频繁输出
        std::atomic<int> completed_count{0};

        for (int i = 0; i < request_count; ++i) {
            co_await tasks[i]; // 等待第i个任务完成（Task<int>，结果不需要）
            completed_count.fetch_add(1);

            // 大幅减少输出频率，提高性能 - 只在关键节点输出
            if (request_count >= 100000) {
                // 超大规模任务：只在25%, 50%, 75%, 100%输出
                int progress = (i + 1) * 100 / request_count;
                if (progress % 25 == 0 && (i + 1) % (request_count / 4) == 0) {
                    std::cout << " 完成进度: " << progress << "%" << std::endl;
                }
            } else if (request_count >= 10000) {
                // 大规模任务：只在10%间隔输出
                if ((i + 1) % (request_count / 10) == 0) {
                    std::cout << " 完成 " << (i + 1) << "/" << request_count 
                              << " (" << ((i + 1) * 100 / request_count) << "%)" << std::endl;
                }
            } else if (request_count >= 1000) {
                // 中等规模：每500个输出一次
                if ((i + 1) % 500 == 0 || (i + 1) == request_count) {
                    std::cout << " 完成 " << (i + 1) << "/" << request_count << std::endl;
                }
            } else {
                // 小规模：每100个输出一次
                if ((i + 1) % 100 == 0 || (i + 1) == request_count) {
                    std::cout << " 完成 " << (i + 1) << "/" << request_count << std::endl;
                }
            }
        }

        std::cout << " 所有任务处理完成，总计: " << completed_count.load() << " 个" << std::endl;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " 纯协程调度完成！" << std::endl;
    std::cout << " 总请求数: " << request_count << " 个" << std::endl;
    std::cout << " 总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;

    if (request_count > 0) {
        std::cout << " 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    }

    if (duration.count() > 0) {
        std::cout << " 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    }

    std::cout << " 内存变化: " << SystemInfo::format_memory_bytes(initial_memory)
              << " → " << SystemInfo::format_memory_bytes(final_memory)
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;

    if (request_count > 0) {
        std::cout << " 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    }

    std::cout << " 并发策略: 协程真正并发 (Task创建即执行)" << std::endl;
    std::cout << " 管理方式: Task内置协程池自动处理" << std::endl;

    // 主动清理协程资源，避免静态对象析构顺序问题
    std::cout << " 清理协程资源..." << std::endl;
    std::cout << " 等待资源清理..." << std::endl;
    
    // 输出JSON结果到文件
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "duration_ms", duration.count());
    cJSON_AddNumberToObject(json, "request_count", request_count);
    if (duration.count() > 0) {
        cJSON_AddNumberToObject(json, "throughput_rps", (double)(request_count * 1000) / duration.count());
    } else {
        cJSON_AddNumberToObject(json, "throughput_rps", 0);
    }
    if (request_count > 0) {
        cJSON_AddNumberToObject(json, "avg_latency_ms", (double)duration.count() / request_count);
    } else {
        cJSON_AddNumberToObject(json, "avg_latency_ms", 0);
    }
    cJSON_AddNumberToObject(json, "memory_usage_bytes", final_memory);
    cJSON_AddNumberToObject(json, "memory_delta_bytes", memory_delta);
    cJSON_AddStringToObject(json, "mode", "coroutine");
    cJSON_AddNumberToObject(json, "exit_code", 0);
    
    char *json_string = cJSON_Print(json);
    std::string result_path = project_root + "/coroutine_result.json";
    std::ofstream result_file(result_path);
    if (result_file.is_open()) {
        result_file << json_string << std::endl;
        result_file.close();
        std::cout << " JSON结果已保存到 " << result_path << std::endl;
    }
    
    free(json_string);
    cJSON_Delete(json);

    // 强制等待一段时间确保所有异步操作完成
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    co_return;
}
// ─────────────────────────────────────────────────────────────────────────────
// 线程池测试 - 固定 N 个工作线程处理 M 个任务（与协程的 M:N 调度真正对等）
// ─────────────────────────────────────────────────────────────────────────────
void handle_concurrent_requests_threadpool(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    int nthreads = std::thread::hardware_concurrency();
    std::cout << " 线程池方式：" << nthreads << " 个工作线程处理 " << request_count << " 个任务" << std::endl;
    std::cout << " 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << " 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    // 任务队列
    std::queue<int> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<int> completed{0};
    bool all_submitted = false;

    // 填入所有任务
    for (int i = 0; i < request_count; ++i) task_queue.push(i);

    // 启动固定数量的工作线程
    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                int task_id = -1;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    cv.wait(lock, [&]{ return !task_queue.empty() || all_submitted; });
                    if (task_queue.empty()) break;
                    task_id = task_queue.front();
                    task_queue.pop();
                }
                // 与协程侧相同的工作量：整型计算，无堆分配
                volatile int result = 1000 + task_id;
                (void)result;
                int cur = completed.fetch_add(1) + 1;
                if (request_count >= 10000) {
                    if (cur % (request_count / 10) == 0 || cur == request_count)
                        std::cout << " 完成 " << cur << "/" << request_count << std::endl;
                }
            }
        });
    }

    // 通知所有线程有任务
    { std::lock_guard<std::mutex> lk(queue_mutex); all_submitted = true; }
    cv.notify_all();

    for (auto& w : workers) w.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " 线程池方式完成！" << std::endl;
    std::cout << " 工作线程数: " << nthreads << " 个" << std::endl;
    std::cout << " 总请求数: " << request_count << " 个" << std::endl;
    std::cout << " 总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;
    if (request_count > 0)
        std::cout << " 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    if (duration.count() > 0)
        std::cout << " 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << " 内存变化: +" << SystemInfo::format_memory_bytes(memory_delta) << std::endl;

    // 写 JSON 结果
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "duration_ms", duration.count());
    cJSON_AddNumberToObject(json, "request_count", request_count);
    cJSON_AddNumberToObject(json, "throughput_rps",
        duration.count() > 0 ? (double)(request_count * 1000) / duration.count() : 0);
    cJSON_AddNumberToObject(json, "avg_latency_ms",
        request_count > 0 ? (double)duration.count() / request_count : 0);
    cJSON_AddNumberToObject(json, "memory_usage_bytes", final_memory);
    cJSON_AddNumberToObject(json, "memory_delta_bytes", memory_delta);
    cJSON_AddStringToObject(json, "mode", "threadpool");
    cJSON_AddNumberToObject(json, "thread_count", nthreads);
    cJSON_AddNumberToObject(json, "exit_code", 0);
    char *js = cJSON_Print(json);
    std::string result_path = project_root + "/threadpool_result.json";
    std::ofstream rf(result_path);
    if (rf.is_open()) { rf << js << std::endl; rf.close(); }
    free(js);
    cJSON_Delete(json);
}

// ─────────────────────────────────────────────────────────────────────────────
// 原始 one-thread-per-task 测试（保留用于演示线程创建开销）
// ─────────────────────────────────────────────────────────────────────────────
void handle_concurrent_requests_threads(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    std::cout << " 多线程方式：处理 " << request_count << " 个并发请求" << std::endl;
    std::cout << " 初始内存: " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << " CPU核心数: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << " 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::cout << " 同时启动 " << request_count << " 个线程..." << std::endl;

    std::atomic<int> completed{0};
    std::vector<std::thread> threads;

    // 创建与协程数量相同的线程
    for (int i = 0; i < request_count; ++i) {
        threads.emplace_back([i, &completed, request_count]() {
            // 模拟IO操作
            // std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // [公平对比] 与协程侧保持相同的工作量：仅做整型计算，无字符串堆分配
            // 原来的 std::string result = "用户" + std::to_string(1000 + i) + " (已处理)"
            // 是死代码（result 从未被读取），且产生 SSO 溢出堆分配，对线程侧不公平。
            volatile int result = 1000 + i; // volatile 防止被优化掉
            (void)result;

            int current_completed = completed.fetch_add(1) + 1;
            
            // 优化输出频率，提高性能
            bool should_print = false;
            if (request_count >= 10000) {
                // 大规模：每5000个输出一次
                should_print = (current_completed % 5000 == 0 || current_completed == request_count);
            } else if (request_count >= 1000) {
                // 中等规模：每500个输出一次  
                should_print = (current_completed % 500 == 0 || current_completed == request_count);
            } else {
                // 小规模：每100个输出一次
                should_print = (current_completed % 100 == 0 || current_completed == request_count);
            }
            
            if (should_print) {
                std::cout << " 已完成 " << current_completed << "/" << request_count
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
    std::cout << " 多线程方式完成！" << std::endl;
    std::cout << " 总请求数: " << request_count << " 个" << std::endl;
    std::cout << " 总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;
    std::cout << " 平均耗时: " << (double)duration.count() / request_count << " ms/请求" << std::endl;
    std::cout << " 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << " 内存变化: " << SystemInfo::format_memory_bytes(initial_memory)
              << " → " << SystemInfo::format_memory_bytes(final_memory)
              << " (增加 " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    std::cout << " 单请求内存: " << (memory_delta / request_count) << " bytes/请求" << std::endl;
    std::cout << " 线程总数: " << request_count << " 个" << std::endl;
    
    // 输出JSON结果到文件
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "duration_ms", duration.count());
    cJSON_AddNumberToObject(json, "request_count", request_count);
    if (duration.count() > 0) {
        cJSON_AddNumberToObject(json, "throughput_rps", (double)(request_count * 1000) / duration.count());
    } else {
        cJSON_AddNumberToObject(json, "throughput_rps", 0);
    }
    if (request_count > 0) {
        cJSON_AddNumberToObject(json, "avg_latency_ms", (double)duration.count() / request_count);
    } else {
        cJSON_AddNumberToObject(json, "avg_latency_ms", 0);
    }
    cJSON_AddNumberToObject(json, "memory_usage_bytes", final_memory);
    cJSON_AddNumberToObject(json, "memory_delta_bytes", memory_delta);
    cJSON_AddStringToObject(json, "mode", "thread");
    cJSON_AddNumberToObject(json, "exit_code", 0);
    
    char *json_string = cJSON_Print(json);
    std::string result_path = project_root + "/thread_result.json";
    std::ofstream result_file(result_path);
    if (result_file.is_open()) {
        result_file << json_string << std::endl;
        result_file.close();
        std::cout << " JSON结果已保存到 " << result_path << std::endl;
    }
    
    free(json_string);
    cJSON_Delete(json);
}

// ─────────────────────────────────────────────────────────────────────────────
// IO 密集型：协程版 —— 所有任务同时发起 1ms 等待，并发挂起，总耗时 ≈ 1ms
// ─────────────────────────────────────────────────────────────────────────────
Task<void> handle_concurrent_requests_coroutine_io(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    std::cout << " 协程 IO 方式：" << request_count << " 个任务，每个模拟 1ms IO 等待" << std::endl;
    std::cout << " 关键：所有任务同时挂起等待，不占线程！" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    // 每个任务 co_await 1ms —— 挂起后不占工作线程
    auto handle_io_request = [](int user_id) -> Task<int> {
        co_await sleep_for(std::chrono::milliseconds(1)); // 模拟 1ms IO
        co_return user_id;
    };

    // 先创建所有任务（它们同步执行到第一个 co_await，全部挂起等待 1ms 定时器）
    std::vector<Task<int>> tasks;
    tasks.reserve(request_count);
    for (int i = 0; i < request_count; ++i)
        tasks.emplace_back(handle_io_request(1000 + i));

    // 此时所有任务已在同时等待 1ms，再依次 co_await 即可
    for (int i = 0; i < request_count; ++i)
        co_await tasks[i];

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " 协程 IO 测试完成！" << std::endl;
    std::cout << " 总任务数: " << request_count << " 个" << std::endl;
    std::cout << " 总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;
    if (duration.count() > 0)
        std::cout << " 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;
    std::cout << " 内存变化: +" << SystemInfo::format_memory_bytes(memory_delta) << std::endl;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "duration_ms", duration.count());
    cJSON_AddNumberToObject(json, "request_count", request_count);
    cJSON_AddNumberToObject(json, "throughput_rps",
        duration.count() > 0 ? (double)(request_count * 1000) / duration.count() : (double)request_count * 1000);
    cJSON_AddNumberToObject(json, "avg_latency_ms",
        request_count > 0 ? (double)duration.count() / request_count : 0);
    cJSON_AddNumberToObject(json, "memory_usage_bytes", final_memory);
    cJSON_AddNumberToObject(json, "memory_delta_bytes", memory_delta);
    cJSON_AddStringToObject(json, "mode", "coroutine-io");
    cJSON_AddNumberToObject(json, "exit_code", 0);
    char *js = cJSON_Print(json);
    std::string result_path = project_root + "/coroutine-io_result.json";
    std::ofstream rf(result_path);
    if (rf.is_open()) { rf << js << std::endl; rf.close(); }
    free(js);
    cJSON_Delete(json);

    co_return;
}

// ─────────────────────────────────────────────────────────────────────────────
// IO 密集型：线程池版 —— N 个线程轮流 sleep(1ms)，总耗时 ≈ ceil(M/N) ms
// ─────────────────────────────────────────────────────────────────────────────
void handle_concurrent_requests_threadpool_io(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    int nthreads = std::thread::hardware_concurrency();
    std::cout << " 线程池 IO 方式：" << nthreads << " 个线程处理 " << request_count
              << " 个任务，每个 sleep 1ms" << std::endl;
    std::cout << " 理论耗时 ≈ ceil(" << request_count << "/" << nthreads
              << ") × 1ms = " << ((request_count + nthreads - 1) / nthreads) << " ms" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::queue<int> task_queue;
    std::mutex queue_mutex;
    std::condition_variable cv;
    std::atomic<int> completed{0};
    bool all_submitted = false;

    for (int i = 0; i < request_count; ++i) task_queue.push(i);

    std::vector<std::thread> workers;
    workers.reserve(nthreads);
    for (int t = 0; t < nthreads; ++t) {
        workers.emplace_back([&]() {
            while (true) {
                int task_id = -1;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    cv.wait(lock, [&]{ return !task_queue.empty() || all_submitted; });
                    if (task_queue.empty()) break;
                    task_id = task_queue.front();
                    task_queue.pop();
                }
                // 线程阻塞 sleep —— 占用这个工作线程整整 1ms
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                volatile int result = 1000 + task_id;
                (void)result;
                completed.fetch_add(1);
            }
        });
    }
    { std::lock_guard<std::mutex> lk(queue_mutex); all_submitted = true; }
    cv.notify_all();
    for (auto& w : workers) w.join();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " 线程池 IO 测试完成！" << std::endl;
    std::cout << " 工作线程数: " << nthreads << " 个" << std::endl;
    std::cout << " 总任务数: " << request_count << " 个" << std::endl;
    std::cout << " 总耗时: " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;
    if (duration.count() > 0)
        std::cout << " 吞吐量: " << (request_count * 1000 / duration.count()) << " 请求/秒" << std::endl;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "duration_ms", duration.count());
    cJSON_AddNumberToObject(json, "request_count", request_count);
    cJSON_AddNumberToObject(json, "throughput_rps",
        duration.count() > 0 ? (double)(request_count * 1000) / duration.count() : 0);
    cJSON_AddNumberToObject(json, "avg_latency_ms",
        request_count > 0 ? (double)duration.count() / request_count : 0);
    cJSON_AddNumberToObject(json, "memory_usage_bytes", final_memory);
    cJSON_AddNumberToObject(json, "memory_delta_bytes", memory_delta);
    cJSON_AddStringToObject(json, "mode", "threadpool-io");
    cJSON_AddNumberToObject(json, "thread_count", nthreads);
    cJSON_AddNumberToObject(json, "exit_code", 0);
    char *js = cJSON_Print(json);
    std::string result_path = project_root + "/threadpool-io_result.json";
    std::ofstream rf(result_path);
    if (rf.is_open()) { rf << js << std::endl; rf.close(); }
    free(js);
    cJSON_Delete(json);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "用法: " << argv[0]
                  << " <coroutine|thread|threadpool|coroutine-io|threadpool-io>"
                     " <request_count> <project_root_dir>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    int request_count = std::stoi(argv[2]);
    std::string project_root = argv[3];

    std::cout << "========================================" << std::endl;
    std::cout << " FlowCoro 高并发性能测试" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "测试模式: " << mode << std::endl;
    std::cout << "请求数量: " << request_count << " 个" << std::endl;
    std::cout << "测试类型: CPU密集型 (无IO延迟)" << std::endl;
    std::string mode_desc = (mode == "coroutine")    ? "协程 M:N 调度"
                           : (mode == "threadpool")   ? "线程池 (N线程M任务)"
                           : (mode == "coroutine-io") ? "协程 M:N 调度 + 1ms IO等待"
                           : (mode == "threadpool-io")? "线程池 + 1ms IO等待"
                           :                            "one-thread-per-task";
    std::cout << "并发方式: " << mode_desc << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        if (mode == "coroutine") {
            sync_wait([&]() {
                return handle_concurrent_requests_coroutine(request_count, project_root);
            });

            // 给系统一点时间来清理资源
            std::cout << " 等待资源清理..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << " 协程测试完成" << std::endl;

            // 快速退出，避免静态对象析构问题
            std::cout << " 程序结束: [" << SystemInfo::get_current_time() << "]" << std::endl;
            std::quick_exit(0);

        } else if (mode == "thread") {
            handle_concurrent_requests_threads(request_count, project_root);
            std::cout << " 线程测试完成" << std::endl;
        } else if (mode == "threadpool") {
            handle_concurrent_requests_threadpool(request_count, project_root);
            std::cout << " 线程池测试完成" << std::endl;
        } else if (mode == "coroutine-io") {
            sync_wait([&]() {
                return handle_concurrent_requests_coroutine_io(request_count, project_root);
            });
            std::cout << " 等待资源清理..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << " 协程IO测试完成" << std::endl;
            std::cout << " 程序结束: [" << SystemInfo::get_current_time() << "]" << std::endl;
            std::quick_exit(0);
        } else if (mode == "threadpool-io") {
            handle_concurrent_requests_threadpool_io(request_count, project_root);
            std::cout << " 线程池IO测试完成" << std::endl;
        } else {
            std::cerr << " 无效的模式: " << mode
                      << " (支持: coroutine, thread, threadpool, coroutine-io, threadpool-io)" << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << " 出现错误: " << e.what() << std::endl;
        return 1;
    }

    std::cout << " 程序正常结束: [" << SystemInfo::get_current_time() << "]" << std::endl;

    return 0;
}
