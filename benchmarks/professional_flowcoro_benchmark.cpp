#include <flowcoro.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <fstream>

using namespace flowcoro;

// 高精度性能计时器
class HighResTimer {
private:
    std::chrono::high_resolution_clock::time_point start_time_;
    
public:
    HighResTimer() : start_time_(std::chrono::high_resolution_clock::now()) {}
    
    double elapsed_ns() const {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time_);
        return static_cast<double>(duration.count());
    }
    
    double elapsed_us() const {
        return elapsed_ns() / 1000.0;
    }
    
    double elapsed_ms() const {
        return elapsed_ns() / 1000000.0;
    }
    
    void reset() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
};

// 统计信息
struct BenchmarkStats {
    std::vector<double> measurements;
    double min_ns = 0.0;
    double max_ns = 0.0;
    double mean_ns = 0.0;
    double median_ns = 0.0;
    double stddev_ns = 0.0;
    double p95_ns = 0.0;
    double p99_ns = 0.0;
    
    void calculate() {
        if (measurements.empty()) return;
        
        std::sort(measurements.begin(), measurements.end());
        
        min_ns = measurements.front();
        max_ns = measurements.back();
        mean_ns = std::accumulate(measurements.begin(), measurements.end(), 0.0) / measurements.size();
        
        size_t n = measurements.size();
        median_ns = n % 2 == 0 ? 
            (measurements[n/2-1] + measurements[n/2]) / 2.0 : 
            measurements[n/2];
        
        p95_ns = measurements[static_cast<size_t>(n * 0.95)];
        p99_ns = measurements[static_cast<size_t>(n * 0.99)];
        
        // 计算标准差
        double variance = 0.0;
        for (double measurement : measurements) {
            variance += (measurement - mean_ns) * (measurement - mean_ns);
        }
        stddev_ns = std::sqrt(variance / measurements.size());
    }
};

// 基准测试结果
class BenchmarkResult {
public:
    std::string name;
    BenchmarkStats stats;
    size_t iterations;
    double total_time_ns;
    
    BenchmarkResult(const std::string& benchmark_name, size_t iter_count) 
        : name(benchmark_name), iterations(iter_count), total_time_ns(0.0) {}
    
    void add_measurement(double time_ns) {
        stats.measurements.push_back(time_ns);
        total_time_ns += time_ns;
    }
    
    void finalize() {
        stats.calculate();
    }
    
    void print_summary() const {
        std::cout << std::left << std::setw(30) << name 
                  << std::right << std::setw(10) << iterations
                  << std::setw(12) << std::fixed << std::setprecision(0) << stats.mean_ns << " ns"
                  << std::setw(12) << std::fixed << std::setprecision(0) << stats.median_ns << " ns"
                  << std::setw(14) << std::fixed << std::setprecision(2) << (1e9 / stats.mean_ns) << " ops/sec"
                  << std::endl;
    }
    
    void print_detailed() const {
        std::cout << "\n" << name << " - Detailed Statistics:\n";
        std::cout << "  Iterations:    " << iterations << "\n";
        std::cout << "  Mean:          " << std::fixed << std::setprecision(0) << stats.mean_ns << " ns\n";
        std::cout << "  Median:        " << std::fixed << std::setprecision(0) << stats.median_ns << " ns\n";
        std::cout << "  Min:           " << std::fixed << std::setprecision(0) << stats.min_ns << " ns\n";
        std::cout << "  Max:           " << std::fixed << std::setprecision(0) << stats.max_ns << " ns\n";
        std::cout << "  Std Dev:       " << std::fixed << std::setprecision(0) << stats.stddev_ns << " ns\n";
        std::cout << "  95th pct:      " << std::fixed << std::setprecision(0) << stats.p95_ns << " ns\n";
        std::cout << "  99th pct:      " << std::fixed << std::setprecision(0) << stats.p99_ns << " ns\n";
        std::cout << "  Throughput:    " << std::fixed << std::setprecision(2) << (1e9 / stats.mean_ns) << " ops/sec\n";
    }
};

// 基准测试框架
class BenchmarkRunner {
private:
    static constexpr size_t WARMUP_ITERATIONS = 10;
    static constexpr size_t MIN_ITERATIONS = 100;
    static constexpr size_t MAX_ITERATIONS = 10000;
    static constexpr double MIN_BENCHMARK_TIME_NS = 1e8; // 100ms minimum
    
public:
    template<typename Func>
    static BenchmarkResult run(const std::string& name, Func&& benchmark_func) {
        // 预热阶段
        for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
            benchmark_func();
        }
        
        BenchmarkResult result(name, 0);
        HighResTimer total_timer;
        
        // 动态确定迭代次数
        size_t iterations = MIN_ITERATIONS;
        double elapsed = 0.0;
        
        while (elapsed < MIN_BENCHMARK_TIME_NS && iterations <= MAX_ITERATIONS) {
            for (size_t i = 0; i < iterations; ++i) {
                HighResTimer timer;
                benchmark_func();
                result.add_measurement(timer.elapsed_ns());
            }
            
            elapsed = total_timer.elapsed_ns();
            if (elapsed < MIN_BENCHMARK_TIME_NS) {
                iterations = std::min(iterations * 2, MAX_ITERATIONS);
            }
        }
        
        result.iterations = result.stats.measurements.size();
        result.finalize();
        return result;
    }
};

// 测试用协程
Task<int> simple_coroutine(int value) {
    co_return value * 2;
}

Task<void> void_coroutine() {
    co_return;
}

Task<void> sleep_coroutine(std::chrono::nanoseconds duration) {
    co_await sleep_for(duration);
}

Task<int> compute_intensive_coroutine(int iterations) {
    volatile int sum = 0;
    for (int i = 0; i < iterations; ++i) {
        sum += i;
    }
    co_return sum;
}

// 基准测试函数
BenchmarkResult benchmark_coroutine_creation() {
    return BenchmarkRunner::run("Coroutine Creation", []() {
        auto coro = simple_coroutine(42);
        // 只测试创建，不执行
    });
}

BenchmarkResult benchmark_coroutine_execution() {
    return BenchmarkRunner::run("Coroutine Execution", []() {
        auto result = sync_wait([]() {
            return simple_coroutine(42);
        });
        static_cast<void>(result);
    });
}

BenchmarkResult benchmark_void_coroutine() {
    return BenchmarkRunner::run("Void Coroutine", []() {
        sync_wait([]() {
            return void_coroutine();
        });
    });
}

BenchmarkResult benchmark_sleep_1us() {
    return BenchmarkRunner::run("Sleep 1us", []() {
        sync_wait([]() {
            return sleep_coroutine(std::chrono::microseconds(1));
        });
    });
}

BenchmarkResult benchmark_when_any_2_tasks() {
    return BenchmarkRunner::run("WhenAny 2 Tasks", []() {
        auto result = sync_wait([]() -> Task<int> {
            auto task1 = compute_intensive_coroutine(100);
            auto task2 = compute_intensive_coroutine(200);
            auto [value, index] = co_await when_any(std::move(task1), std::move(task2));
            co_return value;
        });
        static_cast<void>(result);
    });
}

BenchmarkResult benchmark_when_any_4_tasks() {
    return BenchmarkRunner::run("WhenAny 4 Tasks", []() {
        auto result = sync_wait([]() -> Task<int> {
            auto task1 = compute_intensive_coroutine(100);
            auto task2 = compute_intensive_coroutine(200);
            auto task3 = compute_intensive_coroutine(150);
            auto task4 = compute_intensive_coroutine(300);
            auto [value, index] = co_await when_any(
                std::move(task1), std::move(task2), 
                std::move(task3), std::move(task4));
            co_return value;
        });
        static_cast<void>(result);
    });
}

BenchmarkResult benchmark_lockfree_queue() {
    return BenchmarkRunner::run("LockFree Queue Ops", []() {
        static lockfree::Queue<int> queue;
        static int counter = 0;
        
        // 入队
        queue.enqueue(++counter);
        
        // 出队
        int value;
        queue.dequeue(value);
    });
}

// Echo服务器基准测试
class EchoServerBenchmark {
private:
    static constexpr uint16_t ECHO_PORT = 8080;
    static constexpr size_t BUFFER_SIZE = 1024;
    static constexpr const char* TEST_MESSAGE = "Hello, Echo Server!";
    
    std::atomic<bool> server_running_{false};
    std::atomic<size_t> requests_handled_{0};
    
public:
    // 简单的TCP Echo服务器协程
    Task<void> echo_server() {
        server_running_.store(true);
        
        // 模拟服务器处理逻辑
        while (server_running_.load()) {
            // 模拟接受连接和处理请求
            co_await sleep_for(std::chrono::microseconds(100));
            requests_handled_.fetch_add(1);
            
            // 模拟echo处理 - 在实际实现中这里会是真正的网络IO
            volatile char buffer[BUFFER_SIZE];
            std::memcpy(const_cast<char*>(buffer), TEST_MESSAGE, std::strlen(TEST_MESSAGE));
        }
        co_return;
    }
    
    // 模拟客户端连接
    Task<void> echo_client() {
        // 模拟客户端发送请求
        co_await sleep_for(std::chrono::microseconds(50));
        
        // 模拟网络延迟和数据传输
        volatile char send_buffer[BUFFER_SIZE];
        volatile char recv_buffer[BUFFER_SIZE];
        std::memcpy(const_cast<char*>(send_buffer), TEST_MESSAGE, std::strlen(TEST_MESSAGE));
        std::memcpy(const_cast<char*>(recv_buffer), TEST_MESSAGE, std::strlen(TEST_MESSAGE));
        
        co_return;
    }
    
    void stop_server() {
        server_running_.store(false);
    }
    
    size_t get_requests_handled() const {
        return requests_handled_.load();
    }
    
    void reset_stats() {
        requests_handled_.store(0);
    }
};

// 高性能数据传输基准测试
class DataTransferBenchmark {
private:
    static constexpr size_t SMALL_BUFFER_SIZE = 64;     // 64B
    static constexpr size_t MEDIUM_BUFFER_SIZE = 4096;  // 4KB  
    static constexpr size_t LARGE_BUFFER_SIZE = 65536;  // 64KB
    
public:
    // 模拟小数据包传输（如聊天消息）
    Task<void> transfer_small_data() {
        char buffer[SMALL_BUFFER_SIZE];
        std::memset(buffer, 'A', SMALL_BUFFER_SIZE);
        
        // 模拟序列化/反序列化
        volatile size_t checksum = 0;
        for (size_t i = 0; i < SMALL_BUFFER_SIZE; ++i) {
            checksum += static_cast<uint8_t>(buffer[i]);
        }
        static_cast<void>(checksum);
        
        co_return;
    }
    
    // 模拟中等数据传输（如文件块）
    Task<void> transfer_medium_data() {
        char buffer[MEDIUM_BUFFER_SIZE];
        std::memset(buffer, 'B', MEDIUM_BUFFER_SIZE);
        
        // 模拟数据校验
        volatile size_t checksum = 0;
        for (size_t i = 0; i < MEDIUM_BUFFER_SIZE; ++i) {
            checksum += static_cast<uint8_t>(buffer[i]);
        }
        static_cast<void>(checksum);
        
        co_return;
    }
    
    // 模拟大数据传输（如视频流）
    Task<void> transfer_large_data() {
        char buffer[LARGE_BUFFER_SIZE];
        std::memset(buffer, 'C', LARGE_BUFFER_SIZE);
        
        // 模拟压缩算法
        volatile size_t compressed_size = 0;
        for (size_t i = 0; i < LARGE_BUFFER_SIZE; i += 64) {
            // 简单的重复检测模拟
            if (i > 0 && buffer[i] == buffer[i-64]) {
                compressed_size += 1; // 压缩标记
            } else {
                compressed_size += 64; // 原始数据
            }
        }
        static_cast<void>(compressed_size);
        
        co_return;
    }
};

BenchmarkResult benchmark_echo_server_throughput() {
    return BenchmarkRunner::run("Echo Server Throughput", []() {
        static EchoServerBenchmark benchmark;
        
        // 启动echo服务器（模拟）
        auto server_task = benchmark.echo_server();
        
        // 发送客户端请求
        sync_wait([&]() -> Task<void> {
            co_await benchmark.echo_client();
        });
        
        // 停止服务器
        benchmark.stop_server();
    });
}

BenchmarkResult benchmark_concurrent_echo_clients() {
    return BenchmarkRunner::run("Concurrent Echo Clients", []() {
        static EchoServerBenchmark benchmark;
        static constexpr size_t CLIENT_COUNT = 10;
        
        benchmark.reset_stats();
        
        sync_wait([&]() -> Task<void> {
            // 启动服务器
            auto server_task = benchmark.echo_server();
            
            // 创建多个并发客户端
            std::vector<Task<void>> client_tasks;
            client_tasks.reserve(CLIENT_COUNT);
            
            for (size_t i = 0; i < CLIENT_COUNT; ++i) {
                client_tasks.emplace_back(benchmark.echo_client());
            }
            
            // 等待所有客户端完成
            for (auto& client : client_tasks) {
                co_await client;
            }
            
            benchmark.stop_server();
            co_return;
        });
    });
}

BenchmarkResult benchmark_small_data_transfer() {
    return BenchmarkRunner::run("Small Data Transfer (64B)", []() {
        static DataTransferBenchmark benchmark;
        sync_wait([&]() -> Task<void> {
            co_await benchmark.transfer_small_data();
        });
    });
}

BenchmarkResult benchmark_medium_data_transfer() {
    return BenchmarkRunner::run("Medium Data Transfer (4KB)", []() {
        static DataTransferBenchmark benchmark;
        sync_wait([&]() -> Task<void> {
            co_await benchmark.transfer_medium_data();
        });
    });
}

BenchmarkResult benchmark_large_data_transfer() {
    return BenchmarkRunner::run("Large Data Transfer (64KB)", []() {
        static DataTransferBenchmark benchmark;
        sync_wait([&]() -> Task<void> {
            co_await benchmark.transfer_large_data();
        });
    });
}

// 网络性能测试：模拟HTTP请求处理
BenchmarkResult benchmark_http_request_processing() {
    return BenchmarkRunner::run("HTTP Request Processing", []() {
        sync_wait([]() -> Task<void> {
            // 模拟HTTP请求解析
            std::string request_data = "GET /api/data HTTP/1.1\r\nHost: localhost\r\n\r\n";
            
            // 模拟请求处理逻辑
            volatile size_t content_length = request_data.length();
            
            // 模拟响应生成
            std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
            volatile size_t response_length = response.length();
            
            // 避免编译器优化
            static_cast<void>(content_length);
            static_cast<void>(response_length);
            
            co_return;
        });
    });
}

void print_system_info() {
    std::cout << "\n=== System Information ===" << std::endl;
    std::cout << "FlowCoro Version: " << FLOWCORO_VERSION_STRING << std::endl;
    std::cout << "Compiler: " << __VERSION__ << std::endl;
    std::cout << "Build Type: " << 
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
        << std::endl;
    std::cout << "Thread Count: " << std::thread::hardware_concurrency() << std::endl;
    std::cout << std::string(50, '=') << std::endl;
}

void print_benchmark_header() {
    std::cout << "\n=== FlowCoro Performance Benchmarks ===" << std::endl;
    std::cout << std::string(100, '=') << std::endl;
    std::cout << std::left << std::setw(30) << "Benchmark Name"
              << std::right << std::setw(10) << "Iterations"
              << std::setw(12) << "Mean Time"
              << std::setw(12) << "Median Time"
              << std::setw(14) << "Throughput"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;
}

void print_benchmark_footer() {
    std::cout << std::string(100, '=') << std::endl;
    std::cout << "\nBenchmark completed successfully." << std::endl;
    std::cout << "Note: Results may vary based on system load and hardware configuration." << std::endl;
}

void save_benchmark_results_json(const std::vector<BenchmarkResult>& results) {
    std::ofstream json_file("benchmark_results.json");
    if (!json_file.is_open()) return;
    
    json_file << "{\n";
    json_file << "  \"benchmark_info\": {\n";
    json_file << "    \"flowcoro_version\": \"" << FLOWCORO_VERSION_STRING << "\",\n";
    json_file << "    \"compiler\": \"" << __VERSION__ << "\",\n";
    json_file << "    \"build_type\": \"" << 
#ifdef NDEBUG
        "Release"
#else
        "Debug"
#endif
        << "\",\n";
    json_file << "    \"thread_count\": " << std::thread::hardware_concurrency() << ",\n";
    json_file << "    \"timestamp\": \"" << std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() << "\"\n";
    json_file << "  },\n";
    json_file << "  \"results\": [\n";
    
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        json_file << "    {\n";
        json_file << "      \"name\": \"" << result.name << "\",\n";
        json_file << "      \"iterations\": " << result.iterations << ",\n";
        json_file << "      \"mean_ns\": " << std::fixed << std::setprecision(0) << result.stats.mean_ns << ",\n";
        json_file << "      \"median_ns\": " << std::fixed << std::setprecision(0) << result.stats.median_ns << ",\n";
        json_file << "      \"min_ns\": " << std::fixed << std::setprecision(0) << result.stats.min_ns << ",\n";
        json_file << "      \"max_ns\": " << std::fixed << std::setprecision(0) << result.stats.max_ns << ",\n";
        json_file << "      \"stddev_ns\": " << std::fixed << std::setprecision(0) << result.stats.stddev_ns << ",\n";
        json_file << "      \"p95_ns\": " << std::fixed << std::setprecision(0) << result.stats.p95_ns << ",\n";
        json_file << "      \"p99_ns\": " << std::fixed << std::setprecision(0) << result.stats.p99_ns << ",\n";
        json_file << "      \"throughput_ops_per_sec\": " << std::fixed << std::setprecision(2) << (1e9 / result.stats.mean_ns) << "\n";
        json_file << "    }";
        if (i < results.size() - 1) json_file << ",";
        json_file << "\n";
    }
    
    json_file << "  ]\n";
    json_file << "}\n";
    json_file.close();
    
    std::cout << "\nBenchmark results saved to benchmark_results.json" << std::endl;
}

int main() {
    print_system_info();
    print_benchmark_header();
    
    std::vector<BenchmarkResult> results;
    
    // Core coroutine benchmarks
    results.emplace_back(benchmark_coroutine_creation());
    results.emplace_back(benchmark_coroutine_execution());
    results.emplace_back(benchmark_void_coroutine());
    
    // Concurrency benchmarks
    results.emplace_back(benchmark_when_any_2_tasks());
    results.emplace_back(benchmark_when_any_4_tasks());
    
    // Memory and data structure benchmarks
    results.emplace_back(benchmark_lockfree_queue());
    
    // Network and IO benchmarks
    results.emplace_back(benchmark_echo_server_throughput());
    results.emplace_back(benchmark_concurrent_echo_clients());
    results.emplace_back(benchmark_http_request_processing());
    
    // Data transfer benchmarks
    results.emplace_back(benchmark_small_data_transfer());
    results.emplace_back(benchmark_medium_data_transfer());
    results.emplace_back(benchmark_large_data_transfer());
    
    // Timing benchmarks
    results.emplace_back(benchmark_sleep_1us());
    
    // Print summary
    for (const auto& result : results) {
        result.print_summary();
    }
    
    print_benchmark_footer();
    
    // Save JSON results
    save_benchmark_results_json(results);
    
    // Print detailed statistics for key benchmarks
    std::cout << "\n=== Detailed Statistics ===" << std::endl;
    for (const auto& result : results) {
        if (result.name.find("Echo") != std::string::npos || 
            result.name.find("HTTP") != std::string::npos ||
            result.name.find("Data Transfer") != std::string::npos ||
            result.name.find("Coroutine Execution") != std::string::npos) {
            result.print_detailed();
        }
    }
    
    return 0;
}
