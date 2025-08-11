#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>
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
    std::cout << " FlowCoro 协程 vs 多线程终极对决" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << " 独立进程测试 - 完全内存隔离" << std::endl;
    std::cout << " 测试规模: " << request_count << " 个并发请求" << std::endl;
    std::cout << " 每个请求: 50ms IO模拟" << std::endl;
    std::cout << " CPU核心数: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << " 主进程内存: " << SystemInfo::format_memory_bytes(SystemInfo::get_memory_usage_bytes()) << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        std::vector<TestResult> results;

        // 测试1：协程方式（独立进程）
        std::cout << " 第一轮：协程方式测试" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto coro_result = run_process_test("coroutine", request_count);
        results.push_back(coro_result);

        // 等待系统稳定
        std::cout << " 等待系统资源释放..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << std::endl;

        // 测试2：多线程方式（独立进程）
        std::cout << " 第二轮：多线程方式测试" << std::endl;
        std::cout << std::string(40, '-') << std::endl;
        auto thread_result = run_process_test("thread", request_count);
        results.push_back(thread_result);

        // 对比分析 - 使用cJSON库生成标准JSON输出
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " 标准化性能分析报告（cJSON格式）" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        // 使用cJSON生成标准JSON
        cJSON *json = cJSON_CreateObject();
        
        // 测试配置
        cJSON *test_config = cJSON_CreateObject();
        cJSON_AddNumberToObject(test_config, "task_count", request_count);
        cJSON_AddNumberToObject(test_config, "cpu_cores", std::thread::hardware_concurrency());
        cJSON_AddStringToObject(test_config, "test_type", "CPU密集型");
        cJSON_AddItemToObject(json, "test_config", test_config);
        
        // 协程结果
        cJSON *coroutine_result = cJSON_CreateObject();
        cJSON_AddNumberToObject(coroutine_result, "duration_ms", coro_result.duration_ms);
        cJSON_AddNumberToObject(coroutine_result, "throughput_rps", coro_result.throughput);
        cJSON_AddNumberToObject(coroutine_result, "avg_latency_ms", coro_result.avg_latency);
        cJSON_AddNumberToObject(coroutine_result, "exit_code", coro_result.exit_code);
        cJSON_AddItemToObject(json, "coroutine_result", coroutine_result);
        
        // 线程结果
        cJSON *thread_result_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(thread_result_json, "duration_ms", thread_result.duration_ms);
        cJSON_AddNumberToObject(thread_result_json, "throughput_rps", thread_result.throughput);
        cJSON_AddNumberToObject(thread_result_json, "avg_latency_ms", thread_result.avg_latency);
        cJSON_AddNumberToObject(thread_result_json, "exit_code", thread_result.exit_code);
        cJSON_AddItemToObject(json, "thread_result", thread_result_json);

        double speed_ratio = (double)thread_result.duration_ms / coro_result.duration_ms;
        std::string winner = (speed_ratio > 1.0) ? "coroutine" : "thread";
        double advantage = (speed_ratio > 1.0) ? speed_ratio : (1.0 / speed_ratio);
        
        // 比较结果
        cJSON *comparison = cJSON_CreateObject();
        cJSON_AddStringToObject(comparison, "winner", winner.c_str());
        cJSON_AddNumberToObject(comparison, "speed_advantage", advantage);
        cJSON_AddNumberToObject(comparison, "performance_improvement_percent", (advantage - 1) * 100);
        cJSON_AddBoolToObject(comparison, "coroutine_faster", speed_ratio > 1.0);
        cJSON_AddItemToObject(json, "comparison", comparison);
        
        // 输出格式化的JSON
        char *json_string = cJSON_Print(json);
        std::cout << "[PERF_REPORT_BEGIN]" << std::endl;
        std::cout << json_string << std::endl;
        std::cout << "[PERF_REPORT_END]" << std::endl;
        
        // 清理内存
        free(json_string);
        cJSON_Delete(json);
        std::cout << std::endl;
        std::cout << " 传统格式性能对比：" << std::endl;
        std::cout << " 协程总耗时： " << coro_result.duration_ms << " ms" << std::endl;
        std::cout << " 多线程总耗时： " << thread_result.duration_ms << " ms" << std::endl;

        if (speed_ratio > 1.0) {
            std::cout << " 协程速度优势： " << std::fixed << std::setprecision(1)
                      << speed_ratio << "x (快 " << std::setprecision(0)
                      << (speed_ratio - 1) * 100 << "%)" << std::endl;
        } else {
            std::cout << " 多线程速度优势： " << std::fixed << std::setprecision(1)
                      << (1.0 / speed_ratio) << "x (快 " << std::setprecision(0)
                      << (1.0 / speed_ratio - 1) * 100 << "%)" << std::endl;
        }

        std::cout << std::endl;
        std::cout << " 吞吐量对比：" << std::endl;
        std::cout << " 协程吞吐量： " << std::fixed << std::setprecision(0)
                  << coro_result.throughput << " 请求/秒" << std::endl;
        std::cout << " 多线程吞吐量： " << std::fixed << std::setprecision(0)
                  << thread_result.throughput << " 请求/秒" << std::endl;

        std::cout << std::endl;
        std::cout << " 平均延迟对比：" << std::endl;
        std::cout << " 协程平均延迟： " << std::fixed << std::setprecision(2)
                  << coro_result.avg_latency << " ms/请求" << std::endl;
        std::cout << " 多线程平均延迟：" << std::fixed << std::setprecision(2)
                  << thread_result.avg_latency << " ms/请求" << std::endl;

        std::cout << std::endl;
        std::cout << " 综合评价：" << std::endl;

        if (coro_result.duration_ms < thread_result.duration_ms) {
            std::cout << " 协程在 " << request_count << " 个并发请求下表现更优！" << std::endl;
            std::cout << " 协程的优势：" << std::endl;
            std::cout << " • 更高的并发效率" << std::endl;
            std::cout << " • 更低的系统开销" << std::endl;
            std::cout << " • 更好的可扩展性" << std::endl;
        } else {
            std::cout << " 多线程在 " << request_count << " 个并发请求下表现更优！" << std::endl;
            std::cout << " 多线程的优势：" << std::endl;
            std::cout << " • 更好的CPU利用率" << std::endl;
            std::cout << " • 更成熟的实现" << std::endl;
            std::cout << " • 更直观的并行模型" << std::endl;
        }

        std::cout << std::endl;
        std::cout << " 使用建议：" << std::endl;
        std::cout << " 协程适用场景：" << std::endl;
        std::cout << " • 高并发服务器（数千到数万连接）" << std::endl;
        std::cout << " • IO密集型应用" << std::endl;
        std::cout << " • 需要大量轻量级任务的场景" << std::endl;
        std::cout << std::endl;
        std::cout << " 多线程适用场景：" << std::endl;
        std::cout << " • CPU密集型任务" << std::endl;
        std::cout << " • 需要真正并行计算的场景" << std::endl;
        std::cout << " • 中等规模的并发需求" << std::endl;

        std::cout << std::endl;
        std::cout << " 想测试更大规模？运行：" << argv[0] << " 1000" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << " 出现错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
