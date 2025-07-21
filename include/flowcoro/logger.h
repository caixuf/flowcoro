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

// 日志级别枚举
enum class LogLevel : int {
    TRACE = 0,
    LOG_DEBUG = 1,
    LOG_INFO = 2,
    LOG_WARN = 3,
    LOG_ERROR = 4,
    LOG_FATAL = 5
};

// 日志条目结构 - 缓存友好设计
struct LogEntry {
    std::chrono::steady_clock::time_point timestamp;
    LogLevel level;
    std::thread::id thread_id;
    char message[128];  // 减小大小
    char file[32];      // 减小大小
    int line;
    
    LogEntry() = default;
    
    LogEntry(LogLevel lvl, const char* msg, const char* filename, int ln) 
        : timestamp(std::chrono::steady_clock::now())
        , level(lvl)
        , thread_id(std::this_thread::get_id())
        , line(ln) {
        
        // 安全拷贝消息
        strncpy(message, msg, sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        
        // 提取文件名（去掉路径）
        const char* basename = strrchr(filename, '/');
        basename = basename ? basename + 1 : filename;
        strncpy(file, basename, sizeof(file) - 1);
        file[sizeof(file) - 1] = '\0';
    }
};

// 高性能无锁日志器
class Logger {
private:
    static constexpr size_t RING_BUFFER_SIZE = 4096;  // 必须是2的幂
    static constexpr size_t BATCH_SIZE = 64;
    
    // 使用无锁环形缓冲区
    lockfree::RingBuffer<LogEntry, RING_BUFFER_SIZE> log_buffer_;
    
    // 后台写入线程
    std::unique_ptr<std::thread> writer_thread_;
    std::atomic<bool> shutdown_{false};
    
    // 输出流
    std::unique_ptr<std::ofstream> file_stream_;
    std::atomic<LogLevel> min_level_{LogLevel::LOG_INFO};
    
    // 性能统计
    alignas(64) std::atomic<uint64_t> total_logs_{0};
    alignas(64) std::atomic<uint64_t> dropped_logs_{0};
    
public:
    Logger() = default;
    
    ~Logger() {
        shutdown();
    }
    
    // 初始化日志器
    bool initialize(const std::string& filename = "", LogLevel min_level = LogLevel::LOG_INFO) {
        min_level_.store(min_level, std::memory_order_release);
        
        if (!filename.empty()) {
            file_stream_ = std::make_unique<std::ofstream>(filename, std::ios::app);
            if (!file_stream_->is_open()) {
                return false;
            }
        }
        
        // 启动后台写入线程
        writer_thread_ = std::make_unique<std::thread>([this] { writer_loop(); });
        
        return true;
    }
    
    // 关闭日志器
    void shutdown() {
        if (shutdown_.exchange(true, std::memory_order_acq_rel)) {
            return; // 已经关闭
        }
        
        if (writer_thread_ && writer_thread_->joinable()) {
            writer_thread_->join();
        }
        
        if (file_stream_) {
            file_stream_->close();
        }
    }
    
    // 记录日志 - 高性能无锁版本
    template<typename... Args>
    void log(LogLevel level, const char* file, int line, const char* format, Args&&... args) {
        if (level < min_level_.load(std::memory_order_acquire)) {
            return;
        }
        
        // 格式化消息
        std::array<char, 512> format_buffer;
        if constexpr (sizeof...(args) > 0) {
            std::snprintf(format_buffer.data(), format_buffer.size(), format, std::forward<Args>(args)...);
        } else {
            // 没有参数时直接复制格式字符串
            std::strncpy(format_buffer.data(), format, format_buffer.size() - 1);
            format_buffer[format_buffer.size() - 1] = '\0';
        }
        
        // 创建日志条目
        LogEntry entry(level, format_buffer.data(), file, line);
        
        // 尝试写入环形缓冲区
        if (!log_buffer_.push(std::move(entry))) {
            dropped_logs_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        
        total_logs_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // 设置日志级别
    void set_level(LogLevel level) {
        min_level_.store(level, std::memory_order_release);
    }
    
    // 获取性能统计
    struct Stats {
        uint64_t total_logs;
        uint64_t dropped_logs;
        double drop_rate;
    };
    
    Stats get_stats() const {
        uint64_t total = total_logs_.load(std::memory_order_acquire);
        uint64_t dropped = dropped_logs_.load(std::memory_order_acquire);
        double drop_rate = total > 0 ? (double)dropped / total * 100.0 : 0.0;
        
        return {total, dropped, drop_rate};
    }
    
private:
    void writer_loop() {
        std::array<LogEntry, BATCH_SIZE> batch;
        
        while (!shutdown_.load(std::memory_order_acquire)) {
            size_t count = 0;
            
            // 批量读取日志条目
            while (count < BATCH_SIZE) {
                if (!log_buffer_.pop(batch[count])) {
                    break;
                }
                ++count;
            }
            
            if (count > 0) {
                write_batch(batch.data(), count);
            } else {
                // 没有日志时稍微休眠
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
        
        // 处理剩余的日志
        LogEntry entry;
        while (log_buffer_.pop(entry)) {
            write_entry(entry);
        }
    }
    
    void write_batch(const LogEntry* entries, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            write_entry(entries[i]);
        }
        
        // 批量刷新
        if (file_stream_) {
            file_stream_->flush();
        } else {
            std::cout.flush();
        }
    }
    
    void write_entry(const LogEntry& entry) {
        // 格式化时间戳
        auto time_since_epoch = entry.timestamp.time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_since_epoch).count();
        
        // 格式化日志级别
        const char* level_str = level_to_string(entry.level);
        
        // 构建日志行
        char log_line[1024];
        std::snprintf(log_line, sizeof(log_line), 
            "[%ld] [%s] [%s:%d] [tid:%zu] %s\n",
            ms, level_str, entry.file, entry.line, 
            std::hash<std::thread::id>{}(entry.thread_id), entry.message);
        
        // 输出到文件或控制台
        if (file_stream_) {
            file_stream_->write(log_line, strlen(log_line));
        } else {
            std::cout << log_line;
        }
    }
    
    static const char* level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::LOG_DEBUG: return "DEBUG";
            case LogLevel::LOG_INFO:  return "INFO ";
            case LogLevel::LOG_WARN:  return "WARN ";
            case LogLevel::LOG_ERROR: return "ERROR";
            case LogLevel::LOG_FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }
};

// 全局日志器实例
class GlobalLogger {
private:
    static std::unique_ptr<Logger> instance_;
    static std::once_flag init_flag_;
    
public:
    static Logger& get() {
        std::call_once(init_flag_, []() {
            instance_ = std::make_unique<Logger>();
            instance_->initialize("flowcoro.log", LogLevel::LOG_INFO);
        });
        return *instance_;
    }
    
    static void shutdown() {
        if (instance_) {
            instance_->shutdown();
            instance_.reset();
        }
    }
};
} // namespace flowcoro

// 便捷宏定义
#define LOG_TRACE(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::TRACE, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_DEBUG, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)  flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_INFO,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...)  flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_WARN,  __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_ERROR, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_FATAL(format, ...) flowcoro::GlobalLogger::get().log(flowcoro::LogLevel::LOG_FATAL, __FILE__, __LINE__, format, ##__VA_ARGS__)
