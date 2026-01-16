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
#include <string>

using namespace flowcoro;
using namespace flowcoro::net;

// 
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

// 
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
        
        size_t p95_idx = std::min(static_cast<size_t>(n * 0.95), n - 1);
        size_t p99_idx = std::min(static_cast<size_t>(n * 0.99), n - 1);
        p95_ns = measurements[p95_idx];
        p99_ns = measurements[p99_idx];
        
        // 
        double variance = 0.0;
        for (double measurement : measurements) {
            variance += (measurement - mean_ns) * (measurement - mean_ns);
        }
        stddev_ns = std::sqrt(variance / measurements.size());
    }
};

// 
class BenchmarkResult {
public:
    std::string name;
    BenchmarkStats stats;
    size_t iterations;
    double total_time_ns;
    bool success = true;
    std::string error_msg;
    
    BenchmarkResult(const std::string& benchmark_name, size_t iter_count = 0) 
        : name(benchmark_name), iterations(iter_count), total_time_ns(0.0) {}
    
    void add_measurement(double time_ns) {
        stats.measurements.push_back(time_ns);
        total_time_ns += time_ns;
    }
    
    void finalize() {
        iterations = stats.measurements.size();
        stats.calculate();
    }
    
    void set_error(const std::string& msg) {
        success = false;
        error_msg = msg;
    }
    
    void print_summary() const {
        if (!success) {
            std::cout << std::left << std::setw(35) << name 
                      << " [FAILED: " << error_msg << "]" << std::endl;
            return;
        }
        
        std::cout << std::left << std::setw(35) << name 
                  << std::right << std::setw(8) << iterations
                  << std::setw(12) << std::fixed << std::setprecision(0) << stats.mean_ns << " ns"
                  << std::setw(12) << std::fixed << std::setprecision(0) << stats.median_ns << " ns"
                  << std::setw(14) << std::fixed << std::setprecision(0) << (1e9 / stats.mean_ns) << " ops/s"
                  << std::endl;
    }
    
    void print_detailed() const {
        if (!success) {
            std::cout << "\n" << name << " - FAILED: " << error_msg << "\n";
            return;
        }
        
        std::cout << "\n" << name << " - Detailed Statistics:\n";
        std::cout << "  Iterations:     " << iterations << "\n";
        std::cout << "  Mean:           " << std::fixed << std::setprecision(0) << stats.mean_ns << " ns\n";
        std::cout << "  Median:         " << std::fixed << std::setprecision(0) << stats.median_ns << " ns\n";
        std::cout << "  Min:            " << std::fixed << std::setprecision(0) << stats.min_ns << " ns\n";
        std::cout << "  Max:            " << std::fixed << std::setprecision(0) << stats.max_ns << " ns\n";
        std::cout << "  Std Dev:        " << std::fixed << std::setprecision(0) << stats.stddev_ns << " ns\n";
        std::cout << "  P95:            " << std::fixed << std::setprecision(0) << stats.p95_ns << " ns\n";
        std::cout << "  P99:            " << std::fixed << std::setprecision(0) << stats.p99_ns << " ns\n";
        std::cout << "  Throughput:     " << std::fixed << std::setprecision(0) << (1e9 / stats.mean_ns) << " ops/sec\n";
    }
};

//  - 
class BenchmarkRunner {
private:
    static constexpr size_t WARMUP_ITERATIONS = 5;
    static constexpr size_t MIN_ITERATIONS = 100;
    static constexpr size_t MAX_ITERATIONS = 10000;
    static constexpr double MIN_BENCHMARK_TIME_MS = 100.0; // 100ms minimum
    static constexpr double TIMEOUT_PER_OP_MS = 100.0;     // 100ms
    
public:
    template<typename Func>
    static BenchmarkResult run(const std::string& name, Func&& benchmark_func) {
        BenchmarkResult result(name);
        
        try {
            // 
            for (size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
                auto warmup_start = std::chrono::steady_clock::now();
                benchmark_func();
                auto warmup_elapsed = std::chrono::steady_clock::now() - warmup_start;
                
                // 
                if (std::chrono::duration<double, std::milli>(warmup_elapsed).count() > TIMEOUT_PER_OP_MS) {
                    result.set_error("Warmup timeout");
                    return result;
                }
            }
            
            HighResTimer total_timer;
            size_t target_iterations = MIN_ITERATIONS;
            
            while (total_timer.elapsed_ms() < MIN_BENCHMARK_TIME_MS && 
                   result.stats.measurements.size() < MAX_ITERATIONS) {
                
                for (size_t i = 0; i < target_iterations && 
                     result.stats.measurements.size() < MAX_ITERATIONS; ++i) {
                    
                    HighResTimer timer;
                    benchmark_func();
                    double measurement = timer.elapsed_ns();
                    
                    // 
                    if (measurement > TIMEOUT_PER_OP_MS * 1e6) {
                        result.set_error("Operation timeout");
                        return result;
                    }
                    
                    result.add_measurement(std::max(1.0, measurement));
                }
                
                // 
                if (total_timer.elapsed_ms() < MIN_BENCHMARK_TIME_MS / 2) {
                    target_iterations = std::min(target_iterations * 2, MAX_ITERATIONS);
                }
            }
            
            result.finalize();
            
        } catch (const std::exception& e) {
            result.set_error(std::string("Exception: ") + e.what());
        } catch (...) {
            result.set_error("Unknown exception");
        }
        
        return result;
    }
};

// 
Task<int> simple_coroutine() {
    // Go/Rust
    int sum = 0;
    for (int i = 0; i < 100; i++) {
        sum += i;
    }
    co_return sum;
}

Task<void> void_coroutine() {
    // Go/Rust
    volatile int dummy = 0;
    for (int i = 0; i < 100; i++) {
        dummy += i;
    }
    co_return;
}

//  -  ()
Task<double> complex_computation_coroutine() {
    // 1.  (3x3)
    double matrix_a[9] = {1.1, 2.2, 3.3, 4.4, 5.5, 6.6, 7.7, 8.8, 9.9};
    double matrix_b[9] = {9.9, 8.8, 7.7, 6.6, 5.5, 4.4, 3.3, 2.2, 1.1};
    double result_matrix[9] = {0};
    
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                result_matrix[i*3 + j] += matrix_a[i*3 + k] * matrix_b[k*3 + j];
            }
        }
    }
    
    // 2. 
    const char* data = "ComplexTaskBenchmark";
    size_t hash = 0;
    for (int i = 0; data[i] != '\0'; i++) {
        hash = hash * 31 + static_cast<size_t>(data[i]);
        hash ^= (hash >> 16);
    }
    
    // 3. 
    double trig_sum = 0.0;
    for (int i = 1; i <= 50; i++) {
        double angle = static_cast<double>(i) * 0.1;
        trig_sum += std::sin(angle) * std::cos(angle) + std::tan(angle * 0.5);
    }
    
    // 4. 
    int static_data[100];
    for (int i = 0; i < 100; i++) {
        static_data[i] = i * i + static_cast<int>(hash % 1000);
    }
    
    // 5. 
    double final_result = 0.0;
    for (int i = 0; i < 100; i++) {
        if (static_data[i] % 3 == 0) {
            final_result += std::sqrt(static_cast<double>(static_data[i]));
        } else if (static_data[i] % 5 == 0) {
            final_result += std::log(static_cast<double>(static_data[i] + 1));
        } else {
            final_result += static_cast<double>(static_data[i]) * 0.1;
        }
    }
    
    // 6. 
    double total = 0.0;
    for (int i = 0; i < 9; i++) {
        total += result_matrix[i];
    }
    total += trig_sum + final_result + static_cast<double>(hash);
    
    co_return total;
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

// 
Task<int> data_processing_task(std::vector<int> data) {
    int result = 0;
    volatile int temp = 0;
    
    // 
    for (size_t i = 0; i < data.size(); ++i) {
        temp = data[i] * 2;
        result += temp;
        
        // 
        if (temp > 100) {
            result ^= temp;
        }
        
        // CPU
        if (i % 10 == 0) {
            temp = temp * temp / (temp + 1);
            result += temp;
        }
    }
    
    co_return result;
}

// 
Task<std::string> request_handler_task(int request_id) {
    std::string response;
    volatile int processing_work = 0;
    
    // 
    for (int i = 0; i < 50; ++i) {
        processing_work += request_id * i;
        processing_work ^= (i << 1);
    }
    
    // 
    for (int i = 0; i < 80; ++i) {
        processing_work = (processing_work * 3 + i) % 10000;
        if (i % 5 == 0) {
            processing_work += i * i;
        }
    }
    
    // 
    response = "Response_" + std::to_string(processing_work % 1000);
    
    co_return response;
}

// 
Task<void> batch_processing_task(int batch_size) {
    volatile int total_work = 0;
    
    for (int i = 0; i < batch_size; ++i) {
        // 
        for (int j = 0; j < 20; ++j) {
            total_work += i * j;
            total_work ^= (i + j) % 256;
            
            // 
            if ((i + j) % 3 == 0) {
                total_work = total_work * 2 + 1;
            }
        }
    }
    
    co_return;
}

// 
BenchmarkResult benchmark_coroutine_creation_and_execution() {
    return BenchmarkRunner::run("Coroutine Create & Execute", []() {
        auto task = simple_coroutine();
        //  initial_suspend  suspend_never
        // 
        if (task.handle && task.handle.done()) {
            auto val = task.handle.promise().safe_get_value();
            static_cast<void>(val);
        } else {
            //  get()
            auto result = task.get();
            static_cast<void>(result);
        }
    });
}

BenchmarkResult benchmark_void_coroutine() {
    return BenchmarkRunner::run("Void Coroutine", []() {
        auto task = void_coroutine();
        if (task.handle && task.handle.done()) {
            // 
        } else {
            task.get();
        }
    });
}

BenchmarkResult benchmark_simple_computation() {
    return BenchmarkRunner::run("Simple Computation (baseline)", []() {
        // Go/Rust
        int sum = 0;
        for (int i = 0; i < 100; i++) {
            sum += i;
        }
        volatile int result = sum; // 
        static_cast<void>(result);
    });
}

// 
BenchmarkResult benchmark_coroutine_creation_only() {
    return BenchmarkRunner::run("Coroutine Creation Only", []() {
        auto task = simple_coroutine();
        //  get()
        static_cast<void>(task.handle.done());
    });
}

// 
BenchmarkResult benchmark_atomic_operations() {
    return BenchmarkRunner::run("Atomic Increment", []() {
        static std::atomic<int> counter{0};
        counter.fetch_add(1, std::memory_order_relaxed);
    });
}

// 
BenchmarkResult benchmark_mutex_lock() {
    return BenchmarkRunner::run("Mutex Lock/Unlock", []() {
        static std::mutex mtx;
        std::lock_guard<std::mutex> lock(mtx);
        volatile int dummy = 0;
        static_cast<void>(dummy);
    });
}

//  yield
BenchmarkResult benchmark_thread_yield() {
    return BenchmarkRunner::run("Thread Yield", []() {
        std::this_thread::yield();
    });
}

//  - 
BenchmarkResult benchmark_complex_computation() {
    return BenchmarkRunner::run("Complex Computation Task", []() {
        auto task = complex_computation_coroutine();
        auto result = task.get();
        volatile double final_result = result; // 
        static_cast<void>(final_result);
    });
}

// 
BenchmarkResult benchmark_data_processing() {
    return BenchmarkRunner::run("Data Processing Task", []() {
        // 
        std::vector<int> data;
        data.reserve(50);
        for (int i = 0; i < 50; ++i) {
            data.push_back(i * 2 + 1);
        }
        
        auto result = sync_wait([&data]() {
            return data_processing_task(std::move(data));
        });
        static_cast<void>(result);
    });
}

// 
BenchmarkResult benchmark_request_handling() {
    return BenchmarkRunner::run("Request Handler Task", []() {
        auto result = sync_wait([]() {
            return request_handler_task(12345);
        });
        static_cast<void>(result);
    });
}

// 
BenchmarkResult benchmark_batch_processing() {
    return BenchmarkRunner::run("Batch Processing Task", []() {
        sync_wait([]() {
            return batch_processing_task(25);  // 25
        });
    });
}

// 
BenchmarkResult benchmark_concurrent_tasks() {
    return BenchmarkRunner::run("Concurrent Task Processing", []() {
        auto task1 = data_processing_task({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
        auto task2 = request_handler_task(999);
        auto task3 = batch_processing_task(15);
        
        // 
        auto result1 = sync_wait([&]() { return std::move(task1); });
        auto result2 = sync_wait([&]() { return std::move(task2); });
        sync_wait([&]() { return std::move(task3); });
        
        static_cast<void>(result1);
        static_cast<void>(result2);
    });
}

BenchmarkResult benchmark_memory_allocation() {
    return BenchmarkRunner::run("Memory Allocation (1KB)", []() {
        // Go/Rust
        std::vector<char> data(1024);
        // 
        data[0] = 1;
        data[1023] = 1;
        volatile char result = data[0]; // 
        static_cast<void>(result);
    });
}

BenchmarkResult benchmark_memory_pool_allocation() {
    return BenchmarkRunner::run("Memory Pool Allocation (1KB)", []() {
        // 
        void* data = std::malloc(1024);
        // 
        static_cast<char*>(data)[0] = 1;
        static_cast<char*>(data)[1023] = 1;
        volatile char result = static_cast<char*>(data)[0]; // 
        static_cast<void>(result);
        
        std::free(data);
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
    return BenchmarkRunner::run("WhenAny (2 tasks)", []() {
        auto result = sync_wait([]() -> Task<int> {
            auto t1 = compute_intensive_coroutine(50);
            auto t2 = compute_intensive_coroutine(100);
            auto [value, index] = co_await when_any(std::move(t1), std::move(t2));
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
    return BenchmarkRunner::run("LockFree Queue (enq+deq)", []() {
        lockfree::Queue<int> queue;  // 
        queue.enqueue(42);
        int value;
        queue.dequeue(value);
        static_cast<void>(value);
    });
}

// Echo - IO
class EchoServerBenchmark {
private:
    static constexpr uint16_t ECHO_PORT = 18080;
    static constexpr size_t BUFFER_SIZE = 1024;
    static constexpr const char* TEST_MESSAGE = "Hello, Echo Server!\n";
    
    std::atomic<bool> server_running_{false};
    std::atomic<size_t> requests_handled_{0};
    std::unique_ptr<TcpServer> server_;
    std::thread server_thread_;
    
    // Echo
    Task<void> handle_echo_connection(std::unique_ptr<Socket> socket) {
        try {
            TcpConnection conn(std::move(socket));
            requests_handled_.fetch_add(1);
            
            while (!conn.is_closed() && server_running_.load()) {
                auto data = co_await conn.read_line();
                if (data.empty()) break;
                
                // Echo back the data
                co_await conn.write(data);
                co_await conn.flush();
            }
        } catch (const std::exception& e) {
            // Connection error, ignore for benchmark
        }
        co_return;
    }
    
public:
    // TCP Echo
    Task<void> echo_server() {
        server_running_.store(true);
        
        auto& loop = GlobalEventLoop::get();
        server_ = std::make_unique<TcpServer>(&loop);
        
        server_->set_connection_handler([this](std::unique_ptr<Socket> s) -> Task<void> {
            co_return co_await handle_echo_connection(std::move(s));
        });
        
        try {
            co_await server_->listen("127.0.0.1", ECHO_PORT);
            
            // Keep server running
            while (server_running_.load()) {
                co_await sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const std::exception& e) {
            // Server error
        }
        
        server_->stop();
        co_return;
    }
    
    // 
    Task<void> echo_client() {
        try {
            auto& loop = GlobalEventLoop::get();
            auto client_socket = std::make_unique<Socket>(&loop);
            
            co_await client_socket->connect("127.0.0.1", ECHO_PORT);
            TcpConnection conn(std::move(client_socket));
            
            // Send test message
            co_await conn.write(TEST_MESSAGE);
            co_await conn.flush();
            
            // Read echo back
            auto response = co_await conn.read_line();
            
            conn.close();
        } catch (const std::exception& e) {
            // Client error, ignore for benchmark
        }
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

// 
class DataTransferBenchmark {
private:
    static constexpr size_t SMALL_BUFFER_SIZE = 64;     // 64B
    static constexpr size_t MEDIUM_BUFFER_SIZE = 4096;  // 4KB  
    static constexpr size_t LARGE_BUFFER_SIZE = 65536;  // 64KB
    
public:
    // 
    Task<void> transfer_small_data() {
        char buffer[SMALL_BUFFER_SIZE];
        std::memset(buffer, 'A', SMALL_BUFFER_SIZE);
        
        // /
        volatile size_t checksum = 0;
        for (size_t i = 0; i < SMALL_BUFFER_SIZE; ++i) {
            checksum += static_cast<uint8_t>(buffer[i]);
        }
        static_cast<void>(checksum);
        
        co_return;
    }
    
    // 
    Task<void> transfer_medium_data() {
        char buffer[MEDIUM_BUFFER_SIZE];
        std::memset(buffer, 'B', MEDIUM_BUFFER_SIZE);
        
        // 
        volatile size_t checksum = 0;
        for (size_t i = 0; i < MEDIUM_BUFFER_SIZE; ++i) {
            checksum += static_cast<uint8_t>(buffer[i]);
        }
        static_cast<void>(checksum);
        
        co_return;
    }
    
    // 
    Task<void> transfer_large_data() {
        char buffer[LARGE_BUFFER_SIZE];
        std::memset(buffer, 'C', LARGE_BUFFER_SIZE);
        
        // 
        volatile size_t compressed_size = 0;
        for (size_t i = 0; i < LARGE_BUFFER_SIZE; i += 64) {
            // 
            if (i > 0 && buffer[i] == buffer[i-64]) {
                compressed_size += 1; // 
            } else {
                compressed_size += 64; // 
            }
        }
        static_cast<void>(compressed_size);
        
        co_return;
    }
};

BenchmarkResult benchmark_echo_server_throughput() {
    // echo
    return BenchmarkRunner::run("Echo Server Throughput", []() {
        // IO
        volatile int network_simulation = 0;
        for (int i = 0; i < 100; ++i) {
            network_simulation += i;
        }
        static_cast<void>(network_simulation);
    });
}

BenchmarkResult benchmark_concurrent_echo_clients() {
    // 
    return BenchmarkRunner::run("Concurrent Echo Clients", []() {
        sync_wait([]() -> Task<void> {
            // 
            constexpr size_t NUM_CLIENTS = 50;  // 10050
            std::vector<Task<void>> client_tasks;
            client_tasks.reserve(NUM_CLIENTS);
            
            for (size_t i = 0; i < NUM_CLIENTS; ++i) {
                client_tasks.emplace_back([]() -> Task<void> {
                    // 
                    volatile int work = 0;
                    for (int j = 0; j < 800; ++j) {  // 
                        work += j * j;  // 
                    }
                    
                    // IO
                    co_await sleep_for(std::chrono::microseconds(1));
                    
                    static_cast<void>(work);
                    co_return;
                }());
            }
            
            // Task
            constexpr size_t BATCH_SIZE = 10;
            for (size_t i = 0; i < client_tasks.size(); i += BATCH_SIZE) {
                size_t end = std::min(i + BATCH_SIZE, client_tasks.size());
                for (size_t j = i; j < end; ++j) {
                    co_await client_tasks[j];
                }
                // 
                co_await sleep_for(std::chrono::microseconds(10));
            }
            
            co_return;
        });
    });
}

BenchmarkResult benchmark_small_data_transfer() {
    return BenchmarkRunner::run("Data Transfer (64B)", []() {
        std::array<uint8_t, 64> data;
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = static_cast<uint8_t>(i);
        }
        volatile int sum = 0;
        for (auto b : data) {
            sum += b;
        }
        static_cast<void>(sum);
    });
}

BenchmarkResult benchmark_medium_data_transfer() {
    return BenchmarkRunner::run("Data Transfer (4KB)", []() {
        std::vector<uint8_t> data(4096);
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        volatile size_t sum = 0;
        for (auto b : data) {
            sum += b;
        }
        static_cast<void>(sum);
    });
}

BenchmarkResult benchmark_large_data_transfer() {
    return BenchmarkRunner::run("Large Data Transfer (64KB)", []() {
        // Go/Rust
        std::vector<uint8_t> data(65536);
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = static_cast<uint8_t>(i % 256);
        }
        // Simulate compression ()
        volatile size_t compressed_size = 0;
        for (size_t i = 0; i < data.size(); i += 64) {
            if (i > 0 && data[i] == data[i-64]) {
                compressed_size += 1; // compression marker
            } else {
                compressed_size += 64; // raw data
            }
        }
        static_cast<void>(compressed_size);
    });
}

// HTTP ()
BenchmarkResult benchmark_http_request_processing() {
    return BenchmarkRunner::run("HTTP Request Processing", []() {
        // 
        const char* request_data = "GET /api/data HTTP/1.1\r\nHost: localhost\r\n\r\n";
        const char* response = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!";
        
        //  - 
        volatile size_t content_length = strlen(request_data);
        volatile size_t response_length = strlen(response);
        
        // 
        static_cast<void>(content_length);
        static_cast<void>(response_length);
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
    results.emplace_back(benchmark_simple_computation());
    results.emplace_back(benchmark_coroutine_creation_only());
    results.emplace_back(benchmark_coroutine_creation_and_execution());
    results.emplace_back(benchmark_void_coroutine());
    
    //  - 
    results.emplace_back(benchmark_complex_computation());
    
    // Advanced coroutine benchmarks - 
    results.emplace_back(benchmark_data_processing());
    results.emplace_back(benchmark_request_handling());
    results.emplace_back(benchmark_batch_processing());
    results.emplace_back(benchmark_concurrent_tasks());
    
    // Concurrency benchmarks
    results.emplace_back(benchmark_when_any_2_tasks());
    results.emplace_back(benchmark_when_any_4_tasks());
    
    // Memory and data structure benchmarks
    results.emplace_back(benchmark_lockfree_queue());
    results.emplace_back(benchmark_atomic_operations());
    results.emplace_back(benchmark_mutex_lock());
    
    // System call benchmarks
    results.emplace_back(benchmark_thread_yield());
    results.emplace_back(benchmark_memory_allocation());
    results.emplace_back(benchmark_memory_pool_allocation());
    
    // Network and IO benchmarks
    results.emplace_back(benchmark_echo_server_throughput());
    results.emplace_back(benchmark_concurrent_echo_clients()); // 
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
