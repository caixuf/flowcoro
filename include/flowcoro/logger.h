#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <sstream>
#include <queue>
#include <array>
#include <iostream>
#include <cstring>
#include <cstdio>
#include "lockfree.h"

namespace flowcoro {

// 
enum class LogLevel : int {
    TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_FATAL = 5
};

// 
enum class LogOutput : int {
    CONSOLE = 0, // 
    FILE = 1, // 
    BOTH = 2 // 
};

//  - 
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::thread::id thread_id;
    char message[128]; // 
    char file[32]; // 
    int line;

    LogEntry() = default;

    LogEntry(LogLevel lvl, const char* msg, const char* filename, int ln)
        : timestamp(std::chrono::system_clock::now())
        , level(lvl)
        , thread_id(std::this_thread::get_id())
        , line(ln) {

        // 
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';

        // 
        const char* basename = strrchr(filename, '/');
        basename = basename ? basename + 1 : filename;
        strncpy(file, basename, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';
    }
};

// 
class Logger {
private:
    static constexpr size_t RING_BUFFER_SIZE = 4096; // 2
    static constexpr size_t BATCH_SIZE = 64;

    // 
    lockfree::RingBuffer<LogEntry, RING_BUFFER_SIZE> log_buffer_;

    // 
    std::unique_ptr<std::thread> writer_thread_;
    std::atomic<bool> shutdown_{false};

    // 
    std::unique_ptr<std::ofstream> file_stream_;
    std::atomic<LogLevel> min_level_{LogLevel::LOG_INFO};
    std::atomic<LogOutput> output_type_{LogOutput::FILE};
    std::string log_file_path_;

    // 
    alignas(64) std::atomic<uint64_t> total_logs_{0};
    alignas(64) std::atomic<uint64_t> dropped_logs_{0};

public:
    Logger() = default;

    ~Logger() {
        shutdown();
    }

    // 
    bool initialize(const std::string& filename = "", LogLevel min_level = LogLevel::LOG_INFO, LogOutput output = LogOutput::FILE) {
        min_level_.store(min_level, std::memory_order_release);
        output_type_.store(output, std::memory_order_release);
        log_file_path_ = filename;

        // 
        if (output == LogOutput::FILE || output == LogOutput::BOTH) {
            if (!filename.empty()) {
                file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
                if (!file_stream_->is_open()) {
                    std::cerr << "Failed to open log file: " << filename << std::endl;
                    return false;
                }
            } else if (output == LogOutput::FILE) {
                // 
                log_file_path_ = "flowcoro.log";
                file_stream_ = std::make_unique<std::ofstream>(log_file_path_, std::ios::app);
                if (!file_stream_->is_open()) {
                    std::cerr << "Failed to open default log file: " << log_file_path_ << std::endl;
                    return false;
                }
            }
        }

        // 
        writer_thread_ = std::make_unique<std::thread>([this] { writer_loop(); });

        return true;
    }

    // 
    void shutdown() {
        if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
            return; // 
        }

        if (writer_thread_ && writer_thread_->joinable()) {
            writer_thread_->join();
        }

        if (file_stream_) {
            file_stream_->close();
        }
    }

    //  - 
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const char* format, Args&&... args) {
        if (level < min_level_.load(std::memory_order_acquire)) {
            return;
        }

        // 
        std::array<char, 512> format_buffer;
        if constexpr (sizeof...(args) > 0) {
            std::snprintf(format_buffer.data(), format_buffer.size(), format, std::forward<Args>(args)...);
        } else {
            // 
            std::strncpy(format_buffer.data(), format, format_buffer.size() - 1);
            format_buffer[format_buffer.size() - 1] = '\0';
        }

        // 
        LogEntry entry(level, format_buffer.data(), file, line);

        // 
        if (!log_buffer_.push(std::move(entry))) {
            dropped_logs_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        total_logs_.fetch_add(1, std::memory_order_relaxed);
    }

    // 
    void set_level(LogLevel level) {
        min_level_.store(level, std::memory_order_release);
    }

    // 
    void set_output(LogOutput output) {
        output_type_.store(output, std::memory_order_release);
    }

    // 
    bool set_log_file(const std::string& filename) {
        if (filename.empty()) {
            return false;
        }

        // 
        auto new_stream = std::make_unique<std::ofstream>(filename, std::ios::app);
        if (!new_stream->is_open()) {
            std::cerr << "Failed to open new log file: " << filename << std::endl;
            return false;
        }

        // 
        file_stream_ = std::move(new_stream);
        log_file_path_ = filename;

        std::cout << "Log file switched to: " << filename << std::endl;
        return true;
    }

    // 
    struct Stats {
        uint64_t total_logs;
        uint64_t dropped_logs;
        double drop_rate;
        std::string output_info;
    };

    Stats get_stats() const {
        uint64_t total = total_logs_.load(std::memory_order_acquire);
        uint64_t dropped = dropped_logs_.load(std::memory_order_acquire);
        double drop_rate = total > 0 ? (double)dropped / total * 100.0 : 0.0;

        std::string output_info;
        LogOutput output = output_type_.load(std::memory_order_acquire);
        switch (output) {
            case LogOutput::CONSOLE:
                output_info = "Console only";
                break;
            case LogOutput::FILE:
                output_info = "File: " + log_file_path_;
                break;
            case LogOutput::BOTH:
                output_info = "Console + File: " + log_file_path_;
                break;
        }

        return {total, dropped, drop_rate, output_info};
    }

private:
    void writer_loop() {
        std::array<LogEntry, BATCH_SIZE> batch;

        while (!shutdown_.load(std::memory_order_acquire)) {
            size_t count = 0;

            // 
            while (count < BATCH_SIZE) {
                if (!log_buffer_.pop(batch[count])) {
                    break;
                }
                ++count;
            }

            if (count > 0) {
                write_batch(batch.data(), count);
            } else {
                // 
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // 
        LogOutput output = output_type_.load(std::memory_order_acquire);
        LogEntry entry;
        while (log_buffer_.pop(entry)) {
            write_entry(entry, output);
        }
    }

    void write_batch(const LogEntry* entries, size_t count) {
        LogOutput output = output_type_.load(std::memory_order_acquire);

        for (size_t i = 0; i < count; ++i) {
            write_entry(entries[i], output);
        }

        // 
        if ((output == LogOutput::FILE || output == LogOutput::BOTH) && file_stream_) {
            file_stream_->flush();
        }
        if (output == LogOutput::CONSOLE || output == LogOutput::BOTH) {
            std::cout.flush();
        }
    }

    void write_entry(const LogEntry& entry, LogOutput output) {
        // 
        auto time_t_value = std::chrono::system_clock::to_time_t(entry.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            entry.timestamp.time_since_epoch()) % 1000;

        std::tm* tm_info = std::localtime(&time_t_value);
        char time_buffer[32];
        std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm_info);

        // 
        const char* level_str = level_to_string(entry.level);

        // 
        char log_line[1024];
        std::snprintf(log_line, sizeof(log_line),
            "[%s.%03ld] [%s] [%s:%d] [tid:%zu] %s\n",
            time_buffer, ms.count(), level_str, entry.file, entry.line,
            std::hash<std::thread::id>{}(entry.thread_id), entry.message);

        // 
        switch (output) {
            case LogOutput::CONSOLE:
                std::cout << log_line;
                break;
            case LogOutput::FILE:
                if (file_stream_) {
                    file_stream_->write(log_line, strlen(log_line));
                }
                break;
            case LogOutput::BOTH:
                std::cout << log_line;
                if (file_stream_) {
                    file_stream_->write(log_line, strlen(log_line));
                }
                break;
        }
    }

    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::LOG_DEBUG: return "DEBUG";
            case LogLevel::LOG_INFO: return "INFO ";
            case LogLevel::LOG_WARN: return "WARN ";
            case LogLevel::LOG_ERROR: return "ERROR";
            case LogLevel::LOG_FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
};

// 
class GlobalLogger {
private:
    static std::unique_ptr<Logger> instance_;
    static std::once_flag init_flag_;

public:
    static Logger& get() {
        std::call_once(init_flag_, []() {
            instance_ = std::make_unique<Logger>();
            // 
            instance_->initialize(PROJECT_SOURCE_DIR "/flowcoro.log", LogLevel::LOG_INFO, LogOutput::FILE);
        });
        return *instance_;
    }

    // 
    static bool reinitialize(const std::string& filename = PROJECT_SOURCE_DIR "/flowcoro.log",
                            LogLevel min_level = LogLevel::LOG_INFO,
                            LogOutput output = LogOutput::FILE) {
        if (instance_) {
            instance_->shutdown();
        }
        instance_ = std::make_unique<Logger>();
        return instance_->initialize(filename, min_level, output);
    }

    // 
    static bool set_console_output(LogLevel min_level = LogLevel::LOG_INFO) {
        return reinitialize("", min_level, LogOutput::CONSOLE);
    }

    // 
    static bool set_file_output(const std::string& filename = PROJECT_SOURCE_DIR "/flowcoro.log",
                               LogLevel min_level = LogLevel::LOG_INFO) {
        return reinitialize(filename, min_level, LogOutput::FILE);
    }

    // 
    static bool set_both_output(const std::string& filename = PROJECT_SOURCE_DIR "/flowcoro.log",
                               LogLevel min_level = LogLevel::LOG_INFO) {
        return reinitialize(filename, min_level, LogOutput::BOTH);
    }

    static void shutdown() {
        if (instance_) {
            instance_->shutdown();
            instance_.reset();
        }
    }
};
} // namespace flowcoro

// 
#define LOG_TRACE(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::TRACE, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_INFO, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_WARN, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)
