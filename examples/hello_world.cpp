#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cjson/cJSON.h>

// 系统信息工具
class SystemInfo {
public:
    static size_t get_memory_usage_bytes() {
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.substr(0, 6) == "VmRSS:") {
                std::istringstream iss(line);
                std::string label, value, unit;
                iss >> label >> value >> unit;
                return std::stoul(value) * 1024;
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

// 测试结果结构
struct TestResult {
    std::string method;
    int request_count;
    long duration_ms;
    double throughput;
    double avg_latency;
    int exit_code;
    size_t memory_usage = 0;
    size_t memory_delta = 0;
    
    // 从JSON文件读取结果
    bool load_from_json(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << " 无法打开结果文件: " << filename << std::endl;
            return false;
        }
        
        std::string json_content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
        file.close();
        
        cJSON *json = cJSON_Parse(json_content.c_str());
        if (!json) {
            std::cerr << " JSON解析失败: " << filename << std::endl;
            return false;
        }
        
        cJSON *duration = cJSON_GetObjectItem(json, "duration_ms");
        cJSON *throughput_item = cJSON_GetObjectItem(json, "throughput_rps");
        cJSON *latency = cJSON_GetObjectItem(json, "avg_latency_ms");
        cJSON *exit_code_item = cJSON_GetObjectItem(json, "exit_code");
        cJSON *memory_usage_item = cJSON_GetObjectItem(json, "memory_usage_bytes");
        cJSON *memory_delta_item = cJSON_GetObjectItem(json, "memory_delta_bytes");
        
        if (duration && cJSON_IsNumber(duration)) {
            duration_ms = (long)cJSON_GetNumberValue(duration);
        }
        if (throughput_item && cJSON_IsNumber(throughput_item)) {
            throughput = cJSON_GetNumberValue(throughput_item);
        }
        if (latency && cJSON_IsNumber(latency)) {
            avg_latency = cJSON_GetNumberValue(latency);
        }
        if (exit_code_item && cJSON_IsNumber(exit_code_item)) {
            exit_code = (int)cJSON_GetNumberValue(exit_code_item);
        }
        if (memory_usage_item && cJSON_IsNumber(memory_usage_item)) {
            memory_usage = (size_t)cJSON_GetNumberValue(memory_usage_item);
        }
        if (memory_delta_item && cJSON_IsNumber(memory_delta_item)) {
            memory_delta = (size_t)cJSON_GetNumberValue(memory_delta_item);
        }
        
        cJSON_Delete(json);
        return true;
    }
};

// 运行独立进程测试
TestResult run_process_test(const std::string& mode, int request_count) {
    TestResult result;
    result.method = mode;
    result.request_count = request_count;

    std::cout << " 启动 " << mode << " 测试进程..." << std::endl;
    std::cout << " 请求数量: " << request_count << " 个" << std::endl;
    std::cout << " 开始时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    // 获取项目根路径 - 使用CMake定义的宏
    std::string project_build_dir = PROJECT_BUILD_DIR;
    std::string project_source_dir = PROJECT_SOURCE_DIR;
    
    // 结果文件保存到项目根目录
    std::string result_file = project_source_dir + "/" + mode + "_result.json";
    std::remove(result_file.c_str());
    
    // 构建命令 - 使用项目构建目录的路径
    std::string command = project_build_dir + "/examples/hello_world_concurrent " + mode + " " + std::to_string(request_count) + " " + project_source_dir;
    
    // 执行命令
    int exit_code = std::system(command.c_str());
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto process_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << std::string(60, '=') << std::endl;
    std::cout << " " << mode << " 测试进程完成" << std::endl;
    std::cout << " 结束时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << " 进程耗时: " << process_duration.count() << " ms" << std::endl;
    
    // 尝试从JSON文件读取结果
    if (result.load_from_json(result_file)) {
        result.exit_code = (exit_code == 0) ? 0 : 1;
        std::cout << " 成功读取JSON结果: " << result_file << std::endl;
    } else {
        std::cerr << " 读取JSON结果失败，使用默认值" << std::endl;
        result.duration_ms = process_duration.count();
        result.exit_code = (exit_code == 0) ? 0 : 1;
        result.throughput = 0;
        result.avg_latency = 0;
    }
    
    std::cout << std::endl;
    return result;
}

int main(int argc, char* argv[]) {
    int request_count = 200; // 默认200个请求

    if (argc >= 2) {
        request_count = std::stoi(argv[1]);
    }

    std::cout << "========================================" << std::endl;
    std::cout << " FlowCoro 三路并发性能对决" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << " 独立进程测试 - 完全内存隔离" << std::endl;
    std::cout << " 测试规模: " << request_count << " 个并发任务" << std::endl;
    std::cout << " 任务体: 纯整型计算（零堆分配，三路公平）" << std::endl;
    std::cout << " CPU核心数: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << " 主进程内存: " << SystemInfo::format_memory_bytes(SystemInfo::get_memory_usage_bytes()) << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        std::vector<TestResult> results;

        // 测试1：协程方式（独立进程）
        std::cout << " 第一轮：协程 M:N 调度" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto coro_result = run_process_test("coroutine", request_count);
        results.push_back(coro_result);

        // 等待系统稳定
        std::cout << " 等待系统资源释放..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << std::endl;

        // 测试2：线程池方式（N线程M任务，与协程公平对比）
        std::cout << " 第二轮：线程池（N线程处理M任务）" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto pool_result = run_process_test("threadpool", request_count);
        results.push_back(pool_result);

        // 等待系统稳定
        std::cout << " 等待系统资源释放..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << std::endl;

        // 测试3：one-thread-per-task（演示 OS 线程创建开销）
        std::cout << " 第三轮：one-thread-per-task" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto thread_result = run_process_test("thread", request_count);
        results.push_back(thread_result);

        // ──────────────────────────────────────────────────
        // 三路对比报告
        // ──────────────────────────────────────────────────
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " 三路性能分析报告" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        // JSON 报告
        cJSON *json = cJSON_CreateObject();

        cJSON *test_config = cJSON_CreateObject();
        cJSON_AddNumberToObject(test_config, "task_count", request_count);
        cJSON_AddNumberToObject(test_config, "cpu_cores", std::thread::hardware_concurrency());
        cJSON_AddStringToObject(test_config, "test_type", "纯整型计算（零堆分配）");
        cJSON_AddItemToObject(json, "test_config", test_config);

        auto add_result = [&](cJSON* root, const char* key, const TestResult& r) {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "duration_ms",   r.duration_ms);
            cJSON_AddNumberToObject(obj, "throughput_rps", r.throughput);
            cJSON_AddNumberToObject(obj, "avg_latency_ms", r.avg_latency);
            cJSON_AddNumberToObject(obj, "exit_code",      r.exit_code);
            cJSON_AddItemToObject(root, key, obj);
        };
        add_result(json, "coroutine_result",  coro_result);
        add_result(json, "threadpool_result", pool_result);
        add_result(json, "thread_result",     thread_result);

        // 计算各组对比（协程耗时为0时用 "<1ms" 显示，避免除零）
        auto safe_ratio = [](long num, long den) -> std::string {
            if (den <= 0) return ">10000";
            double r = (double)num / den;
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1) << r;
            return oss.str();
        };
        double coro_vs_pool   = pool_result.duration_ms   > 0 ? (double)pool_result.duration_ms   / std::max(1L, coro_result.duration_ms) : 0;
        double coro_vs_thread = thread_result.duration_ms > 0 ? (double)thread_result.duration_ms / std::max(1L, coro_result.duration_ms) : 0;

        cJSON *comparison = cJSON_CreateObject();
        cJSON_AddNumberToObject(comparison, "coroutine_vs_threadpool_speedup", coro_vs_pool);
        cJSON_AddNumberToObject(comparison, "coroutine_vs_thread_speedup",     coro_vs_thread);
        cJSON_AddItemToObject(json, "comparison", comparison);

        char *json_string = cJSON_Print(json);
        std::cout << "[PERF_REPORT_BEGIN]" << std::endl;
        std::cout << json_string << std::endl;
        std::cout << "[PERF_REPORT_END]" << std::endl;
        free(json_string);
        cJSON_Delete(json);

        // 文字对比表格
        std::cout << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " 性能对比汇总" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::left
                  << " " << std::setw(28) << "模式"
                  << std::setw(12) << "耗时(ms)"
                  << std::setw(18) << "吞吐量(req/s)"
                  << "相对协程" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        auto print_row = [&](const std::string& label, const TestResult& r, double ratio) {
            std::cout << " " << std::setw(28) << label
                      << std::setw(12) << r.duration_ms
                      << std::setw(18) << std::fixed << std::setprecision(0) << r.throughput;
            if (ratio > 0)
                std::cout << "慢 " << std::setprecision(1) << ratio << "x";
            else
                std::cout << "(基准)";
            std::cout << std::endl;
        };
        print_row("协程 M:N 调度",          coro_result,   0.0);
        print_row("线程池 (" + std::to_string(std::thread::hardware_concurrency()) + "线程)",
                                            pool_result,   coro_vs_pool);
        print_row("one-thread-per-task",    thread_result, coro_vs_thread);
        std::cout << std::string(80, '-') << std::endl;

        std::cout << std::endl;
        std::cout << " 结论：" << std::endl;
        std::cout << " • 协程 vs 线程池   : 协程快 " << safe_ratio(pool_result.duration_ms, std::max(1L, coro_result.duration_ms))
                  << "x —— 调度开销对比（用户态 vs mutex/condvar）" << std::endl;
        std::cout << " • 协程 vs 1线程/任务: 协程快 " << safe_ratio(thread_result.duration_ms, std::max(1L, coro_result.duration_ms))
                  << "x —— 含 OS 线程创建成本" << std::endl;
        std::cout << " • 线程池 vs 1线程/任务: 线程池快 " << std::fixed << std::setprecision(1)
                  << (thread_result.duration_ms > 0 && pool_result.duration_ms > 0
                      ? (double)thread_result.duration_ms / pool_result.duration_ms : 0)
                  << "x —— 线程复用消除创建开销" << std::endl;

        // ─────────────────────────────────────────────────────────────────
        // 第四轮：IO 密集型场景（协程真正的主场）
        // 每个任务 sleep 1ms，协程全部并发挂起，线程池轮流阻塞
        // ─────────────────────────────────────────────────────────────────
        int io_count = std::min(request_count, 2000); // 限制规模：线程池 IO 测试耗时与 M/N 成正比
        int nthreads = std::thread::hardware_concurrency();
        long expected_pool_ms = (long)std::ceil((double)io_count / nthreads); // 理论值

        std::cout << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " IO 密集型场景对比（每任务 1ms 等待，" << io_count << " 个任务）" << std::endl;
        std::cout << " 协程：所有任务同时挂起等待 → 预期 ~1ms" << std::endl;
        std::cout << " 线程池：" << nthreads << " 线程轮流阻塞 → 预期 ~"
                  << expected_pool_ms << "ms (ceil(" << io_count << "/" << nthreads << ")×1ms)" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        std::cout << " 第四轮：协程 IO 并发" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto coro_io_result = run_process_test("coroutine-io", io_count);

        std::cout << " 等待系统资源释放..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        std::cout << std::endl;

        std::cout << " 第五轮：线程池 IO（阻塞 sleep）" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto pool_io_result = run_process_test("threadpool-io", io_count);

        // IO 场景汇总
        std::cout << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " IO 密集型场景汇总" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::left
                  << " " << std::setw(30) << "模式"
                  << std::setw(12) << "耗时(ms)"
                  << std::setw(18) << "吞吐量(req/s)"
                  << "相对协程" << std::endl;
        std::cout << std::string(80, '-') << std::endl;

        auto print_io_row = [&](const std::string& label, const TestResult& r, double ratio) {
            std::cout << " " << std::setw(30) << label
                      << std::setw(12) << r.duration_ms
                      << std::setw(18) << std::fixed << std::setprecision(0) << r.throughput;
            if (ratio > 0) std::cout << "慢 " << std::setprecision(1) << ratio << "x";
            else            std::cout << "(基准)";
            std::cout << std::endl;
        };
        double io_ratio = coro_io_result.duration_ms > 0
            ? (double)pool_io_result.duration_ms / coro_io_result.duration_ms
            : (double)pool_io_result.duration_ms;
        print_io_row("协程 M:N + co_await 1ms",  coro_io_result, 0.0);
        print_io_row("线程池 " + std::to_string(nthreads) + "线程 + sleep 1ms",
                                                  pool_io_result, io_ratio);
        std::cout << std::string(80, '-') << std::endl;
        std::cout << std::endl;
        std::cout << " IO 场景结论：" << std::endl;
        std::cout << " • 协程 co_await 挂起不占线程，" << io_count
                  << " 个任务同时等待 → 总耗时 ≈ 1ms" << std::endl;
        std::cout << " • 线程池 sleep 阻塞占用线程，只能 " << nthreads
                  << " 个任务并行 → 总耗时 ≈ " << expected_pool_ms << "ms" << std::endl;
        std::cout << " • 协程快 " << safe_ratio(pool_io_result.duration_ms,
                                                  std::max(1L, coro_io_result.duration_ms))
                  << "x —— 这才是协程的真正优势场景！" << std::endl;
        std::cout << std::endl;
        std::cout << " 想测试更大规模？运行：" << argv[0] << " 100000" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << " 出现错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
