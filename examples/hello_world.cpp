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

    // 创建管道用于捕获子进程输出
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        std::cerr << " 创建管道失败" << std::endl;
        result.exit_code = -1;
        return result;
    }

    auto start_time = std::chrono::high_resolution_clock::now();

    // 创建子进程执行测试
    pid_t pid = fork();
    if (pid == 0) {
        // 子进程：重定向输出到管道
        close(pipefd[0]); // 关闭读端
        dup2(pipefd[1], STDOUT_FILENO); // 重定向标准输出
        close(pipefd[1]);

        // 执行测试程序
        execl("./examples/hello_world_concurrent", "hello_world_concurrent", mode.c_str(), std::to_string(request_count).c_str(), nullptr);
        exit(1); // 如果execl失败
    } else if (pid > 0) {
        // 父进程：读取子进程输出并等待完成
        close(pipefd[1]); // 关闭写端

        char buffer[4096];
        std::string output;
        ssize_t bytes_read;

        // 读取子进程输出
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            output += buffer;
        }
        close(pipefd[0]);

        // 等待子进程完成
        int status;
        waitpid(pid, &status, 0);
        result.exit_code = WEXITSTATUS(status);

        // 显示子进程输出
        std::cout << output;

        // 尝试多种方式解析执行时间
        std::vector<std::string> time_patterns = {
            " [TIME_MARKER] ",  // 新增的明确标记
            " 总耗时: ",
            "总耗时: ",
            "耗时: ",
            " ms"
        };
        
        bool found_time = false;
        for (const auto& pattern : time_patterns) {
            size_t pos = output.find(pattern);
            if (pos != std::string::npos) {
                // 找到模式后，向前查找数字
                size_t start_pos = pos;
                while (start_pos > 0 && output[start_pos - 1] != ' ' && output[start_pos - 1] != ':') {
                    start_pos--;
                }
                
                // 跳过模式本身，找到数字
                size_t num_start = pos + pattern.length();
                size_t end_pos = output.find(" ms", num_start);
                if (end_pos == std::string::npos) {
                    end_pos = output.find("ms", num_start);
                }
                if (end_pos == std::string::npos) {
                    end_pos = output.find(" ", num_start);
                }
                if (end_pos == std::string::npos) {
                    end_pos = output.find("\n", num_start);
                }
                
                if (end_pos != std::string::npos && end_pos > num_start) {
                    std::string time_str = output.substr(num_start, end_pos - num_start);
                    // 清理字符串，只保留数字和小数点
                    std::string clean_time;
                    for (char c : time_str) {
                        if (std::isdigit(c) || c == '.') {
                            clean_time += c;
                        } else {
                            break; // 遇到非数字字符就停止
                        }
                    }
                    
                    if (!clean_time.empty()) {
                        try {
                            double time_val = std::stod(clean_time);
                            result.duration_ms = static_cast<long>(time_val);
                            found_time = true;
                            std::cerr << " [DEBUG] 成功解析时间: " << clean_time << " -> " << result.duration_ms << " ms (模式: " << pattern << ")" << std::endl;
                            break;
                        } catch (const std::exception& e) {
                            std::cerr << " [DEBUG] 解析失败: '" << clean_time << "' (模式: " << pattern << ") - " << e.what() << std::endl;
                        }
                    }
                }
            }
        }
        
        if (!found_time) {
            std::cerr << " [DEBUG] 在输出中未找到任何时间标记，输出内容：" << std::endl;
            std::cerr << "=== 输出开始 ===" << std::endl;
            std::cerr << output << std::endl;
            std::cerr << "=== 输出结束 ===" << std::endl;
        }
    } else {
        std::cerr << " 创建进程失败" << std::endl;
        close(pipefd[0]);
        close(pipefd[1]);
        result.exit_code = -1;
        return result;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    // 如果没有从子进程解析到时间，使用父进程计时作为备用
    if (result.duration_ms == 0) {
        result.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    }

    // 计算性能指标
    result.throughput = (double)request_count * 1000.0 / result.duration_ms;
    result.avg_latency = (double)result.duration_ms / request_count;

    std::cout << std::string(60, '=') << std::endl;
    std::cout << " " << mode << " 测试进程完成" << std::endl;
    std::cout << " 结束时间: [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << " 进程耗时: " << result.duration_ms << " ms" << std::endl;
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

        // 对比分析
        std::cout << std::string(80, '=') << std::endl;
        std::cout << " 终极对比分析（基于独立进程测试）" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        std::cout << " 性能对比：" << std::endl;
        std::cout << " 协程总耗时： " << coro_result.duration_ms << " ms" << std::endl;
        std::cout << " 多线程总耗时： " << thread_result.duration_ms << " ms" << std::endl;

        double speed_ratio = (double)thread_result.duration_ms / coro_result.duration_ms;
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
