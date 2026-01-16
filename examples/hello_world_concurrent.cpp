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
#include <cjson/cJSON.h>

using namespace flowcoro;

// 
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

//  - Task
Task<void> handle_concurrent_requests_coroutine(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    std::cout << "  " << request_count << " " << std::endl;
    std::cout << " : " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << " CPU: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << " : [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    // FlowCoro
    std::cout << " FlowCoro" << std::endl;

    //  - sleep_forwhen_all
    auto handle_single_request = [](int user_id) -> Task<std::string> {
        // sleep_forwhen_all
        // co_await sleep_for(std::chrono::milliseconds(50));

        // 
        std::string result = "" + std::to_string(user_id) + " ()";
        co_return result;
    };

    std::cout << " ..." << std::endl;
    std::cout << " ..." << std::endl;

    // when_all - 
    if (request_count <= 3) {
        std::cout << " when_all..." << std::endl;

        // when_all
        if (request_count == 1) {
            auto task1 = handle_single_request(1001);
            auto result = co_await task1;
            std::cout << " : " << result << std::endl;
        } else if (request_count == 2) {
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto results = co_await when_all(std::move(task1), std::move(task2));
            auto [r1, r2] = results;
            std::cout << " when_all: " << r1 << ", " << r2 << std::endl;
        } else { // request_count == 3
            auto task1 = handle_single_request(1001);
            auto task2 = handle_single_request(1002);
            auto task3 = handle_single_request(1003);
            std::cout << " when_all..." << std::endl;
            auto results = co_await when_all(std::move(task1), std::move(task2), std::move(task3));
            auto [r1, r2, r3] = results;
            std::cout << " when_all: " << r1 << ", " << r2 << ", " << r3 << std::endl;
        }

    } else if (request_count <= 5) {
        std::cout << " ..." << std::endl;

        // when_all
        for (int i = 1; i <= request_count; ++i) {
            auto task = handle_single_request(1000 + i);
            auto result = co_await task; // 
            std::cout << " " << i << ": " << result << std::endl;
        }
    } else {
        std::cout << " ..." << std::endl;

        // Task
        std::vector<Task<std::string>> tasks;
        tasks.reserve(request_count);

        std::cout << "  ()..." << std::endl;
        
        //  - 
        for (int i = 0; i < request_count; ++i) {
            tasks.emplace_back(handle_single_request(1000 + i));
        }

        std::cout << " ..." << std::endl;

        // 
        std::atomic<int> completed_count{0};

        for (int i = 0; i < request_count; ++i) {
            auto result = co_await tasks[i]; // i
            completed_count.fetch_add(1);

            //  - 
            if (request_count >= 100000) {
                // 25%, 50%, 75%, 100%
                int progress = (i + 1) * 100 / request_count;
                if (progress % 25 == 0 && (i + 1) % (request_count / 4) == 0) {
                    std::cout << " : " << progress << "%" << std::endl;
                }
            } else if (request_count >= 10000) {
                // 10%
                if ((i + 1) % (request_count / 10) == 0) {
                    std::cout << "  " << (i + 1) << "/" << request_count 
                              << " (" << ((i + 1) * 100 / request_count) << "%)" << std::endl;
                }
            } else if (request_count >= 1000) {
                // 500
                if ((i + 1) % 500 == 0 || (i + 1) == request_count) {
                    std::cout << "  " << (i + 1) << "/" << request_count << std::endl;
                }
            } else {
                // 100
                if ((i + 1) % 100 == 0 || (i + 1) == request_count) {
                    std::cout << "  " << (i + 1) << "/" << request_count << std::endl;
                }
            }
        }

        std::cout << " : " << completed_count.load() << " " << std::endl;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " " << std::endl;
    std::cout << " : " << request_count << " " << std::endl;
    std::cout << " : " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;

    if (request_count > 0) {
        std::cout << " : " << (double)duration.count() / request_count << " ms/" << std::endl;
    }

    if (duration.count() > 0) {
        std::cout << " : " << (request_count * 1000 / duration.count()) << " /" << std::endl;
    }

    std::cout << " : " << SystemInfo::format_memory_bytes(initial_memory)
              << " → " << SystemInfo::format_memory_bytes(final_memory)
              << " ( " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;

    if (request_count > 0) {
        std::cout << " : " << (memory_delta / request_count) << " bytes/" << std::endl;
    }

    std::cout << " :  (Task)" << std::endl;
    std::cout << " : Task" << std::endl;

    // 
    std::cout << " ..." << std::endl;
    std::cout << " ..." << std::endl;
    
    // JSON
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
        std::cout << " JSON " << result_path << std::endl;
    }
    
    free(json_string);
    cJSON_Delete(json);

    // 
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    co_return;
}
// 
void handle_concurrent_requests_threads(int request_count, const std::string& project_root) {
    auto start_time = std::chrono::high_resolution_clock::now();
    auto initial_memory = SystemInfo::get_memory_usage_bytes();

    std::cout << "  " << request_count << " " << std::endl;
    std::cout << " : " << SystemInfo::format_memory_bytes(initial_memory) << std::endl;
    std::cout << " CPU: " << SystemInfo::get_cpu_cores() << std::endl;
    std::cout << " : [" << SystemInfo::get_current_time() << "]" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::cout << "  " << request_count << " ..." << std::endl;

    std::atomic<int> completed{0};
    std::vector<std::thread> threads;

    // 
    for (int i = 0; i < request_count; ++i) {
        threads.emplace_back([i, &completed, request_count]() {
            // IO
            // std::this_thread::sleep_for(std::chrono::milliseconds(50));
            std::string result = "" + std::to_string(1000 + i) + " ()";

            int current_completed = completed.fetch_add(1) + 1;
            
            // 
            bool should_print = false;
            if (request_count >= 10000) {
                // 5000
                should_print = (current_completed % 5000 == 0 || current_completed == request_count);
            } else if (request_count >= 1000) {
                // 500  
                should_print = (current_completed % 500 == 0 || current_completed == request_count);
            } else {
                // 100
                should_print = (current_completed % 100 == 0 || current_completed == request_count);
            }
            
            if (should_print) {
                std::cout << "  " << current_completed << "/" << request_count
                          << "  (" << (current_completed * 100 / request_count) << "%)" << std::endl;
            }
        });
    }

    // 
    for (auto& thread : threads) {
        thread.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    auto final_memory = SystemInfo::get_memory_usage_bytes();
    auto memory_delta = final_memory > initial_memory ? final_memory - initial_memory : 0;

    std::cout << std::string(50, '-') << std::endl;
    std::cout << " " << std::endl;
    std::cout << " : " << request_count << " " << std::endl;
    std::cout << " : " << duration.count() << " ms" << std::endl;
    std::cout << " [TIME_MARKER] " << duration.count() << " ms [/TIME_MARKER]" << std::endl;
    std::cout << " : " << (double)duration.count() / request_count << " ms/" << std::endl;
    std::cout << " : " << (request_count * 1000 / duration.count()) << " /" << std::endl;
    std::cout << " : " << SystemInfo::format_memory_bytes(initial_memory)
              << " → " << SystemInfo::format_memory_bytes(final_memory)
              << " ( " << SystemInfo::format_memory_bytes(memory_delta) << ")" << std::endl;
    std::cout << " : " << (memory_delta / request_count) << " bytes/" << std::endl;
    std::cout << " : " << request_count << " " << std::endl;
    
    // JSON
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
        std::cout << " JSON " << result_path << std::endl;
    }
    
    free(json_string);
    cJSON_Delete(json);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << ": " << argv[0] << " <coroutine|thread> <request_count> <project_root_dir>" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    int request_count = std::stoi(argv[2]);
    std::string project_root = argv[3];

    std::cout << "========================================" << std::endl;
    std::cout << " FlowCoro " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << ": " << mode << std::endl;
    std::cout << ": " << request_count << " " << std::endl;
    std::cout << ": CPU (IO)" << std::endl;
    std::cout << ": " << (mode == "coroutine" ? "Task" : "") << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    try {
        if (mode == "coroutine") {
            sync_wait([&]() {
                return handle_concurrent_requests_coroutine(request_count, project_root);
            });

            // 
            std::cout << " ..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::cout << " " << std::endl;

            // 
            std::cout << " : [" << SystemInfo::get_current_time() << "]" << std::endl;
            std::quick_exit(0);

        } else if (mode == "thread") {
            handle_concurrent_requests_threads(request_count, project_root);
            std::cout << " " << std::endl;
        } else {
            std::cerr << " : " << mode << " (: coroutine, thread)" << std::endl;
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << " : " << e.what() << std::endl;
        return 1;
    }

    std::cout << " : [" << SystemInfo::get_current_time() << "]" << std::endl;

    return 0;
}
