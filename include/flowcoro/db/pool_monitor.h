#pragma once

#include "connection_pool.h"
#include <chrono>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <unordered_map>
#include <sstream>

// 条件编译JSON支持
#ifdef FLOWCORO_HAS_JSON
#include <json/json.h>
#endif

namespace flowcoro::db {

// 监控指标
struct PoolMetrics {
    // 连接数量指标
    std::atomic<size_t> total_connections{0};
    std::atomic<size_t> active_connections{0};
    std::atomic<size_t> idle_connections{0};
    std::atomic<size_t> failed_connections{0};
    std::atomic<size_t> created_connections{0};
    std::atomic<size_t> destroyed_connections{0};

    // 请求统计
    std::atomic<size_t> total_requests{0};
    std::atomic<size_t> successful_requests{0};
    std::atomic<size_t> failed_requests{0};
    std::atomic<size_t> timeout_requests{0};
    std::atomic<size_t> retry_requests{0};

    // 时间统计 (微秒)
    std::atomic<uint64_t> total_wait_time_us{0};
    std::atomic<uint64_t> max_wait_time_us{0};
    std::atomic<uint64_t> min_wait_time_us{UINT64_MAX};
    std::atomic<uint64_t> total_execution_time_us{0};
    std::atomic<uint64_t> max_execution_time_us{0};

    // 健康检查统计
    std::atomic<size_t> health_checks_performed{0};
    std::atomic<size_t> health_checks_failed{0};
    std::atomic<uint64_t> last_health_check_time{0};

    // 计算平均值
    double get_average_wait_time_ms() const {
        auto total = total_requests.load();
        if (total == 0) return 0.0;
        return static_cast<double>(total_wait_time_us.load()) / (total * 1000.0);
    }

    double get_average_execution_time_ms() const {
        auto total = successful_requests.load();
        if (total == 0) return 0.0;
        return static_cast<double>(total_execution_time_us.load()) / (total * 1000.0);
    }

    double get_success_rate() const {
        auto total = total_requests.load();
        if (total == 0) return 1.0;
        return static_cast<double>(successful_requests.load()) / total;
    }

    double get_health_check_success_rate() const {
        auto total = health_checks_performed.load();
        if (total == 0) return 1.0;
        auto failed = health_checks_failed.load();
        return static_cast<double>(total - failed) / total;
    }

    // 重置统计信息
    void reset() {
        total_requests = 0;
        successful_requests = 0;
        failed_requests = 0;
        timeout_requests = 0;
        retry_requests = 0;
        total_wait_time_us = 0;
        max_wait_time_us = 0;
        min_wait_time_us = UINT64_MAX;
        total_execution_time_us = 0;
        max_execution_time_us = 0;
        health_checks_performed = 0;
        health_checks_failed = 0;
    }
};

// 性能阈值配置
struct PerformanceThresholds {
    std::chrono::milliseconds max_wait_time{5000}; // 最大等待时间
    std::chrono::milliseconds max_execution_time{10000}; // 最大执行时间
    double min_success_rate{0.95}; // 最小成功率
    size_t max_failed_connections{5}; // 最大失败连接数
    std::chrono::minutes alert_interval{5}; // 告警间隔
};

// 告警类型
enum class AlertType {
    HIGH_WAIT_TIME,
    HIGH_EXECUTION_TIME,
    LOW_SUCCESS_RATE,
    TOO_MANY_FAILED_CONNECTIONS,
    CONNECTION_POOL_EXHAUSTED,
    HEALTH_CHECK_FAILED
};

// 告警信息
struct Alert {
    AlertType type;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
    std::string details; // 简化为字符串格式

    Alert(AlertType t, const std::string& msg)
        : type(t), message(msg), timestamp(std::chrono::system_clock::now()) {}
};

// 告警回调函数类型
using AlertCallback = std::function<void(const Alert&)>;

// 连接池监控器
template<typename ConnectionType>
class PoolMonitor {
public:
    PoolMonitor(ConnectionPool<ConnectionType>* pool,
               const PerformanceThresholds& thresholds = {})
        : pool_(pool)
        , thresholds_(thresholds)
        , monitoring_(false)
        , last_alert_time_()
    {
        start_monitoring();
    }

    ~PoolMonitor() {
        stop_monitoring();
    }

    // 设置告警回调
    void set_alert_callback(AlertCallback callback) {
        alert_callback_ = std::move(callback);
    }

    // 获取当前指标
    const PoolMetrics& get_metrics() const {
        return metrics_;
    }

    // 获取JSON格式的指标
#ifdef FLOWCORO_HAS_JSON
    Json::Value get_metrics_json() const {
        Json::Value json;

        // 连接指标
        json["connections"]["total"] = static_cast<Json::UInt64>(metrics_.total_connections.load());
        json["connections"]["active"] = static_cast<Json::UInt64>(metrics_.active_connections.load());
        json["connections"]["idle"] = static_cast<Json::UInt64>(metrics_.idle_connections.load());
        json["connections"]["failed"] = static_cast<Json::UInt64>(metrics_.failed_connections.load());
        json["connections"]["created"] = static_cast<Json::UInt64>(metrics_.created_connections.load());
        json["connections"]["destroyed"] = static_cast<Json::UInt64>(metrics_.destroyed_connections.load());

        // 请求统计
        json["requests"]["total"] = static_cast<Json::UInt64>(metrics_.total_requests.load());
        json["requests"]["successful"] = static_cast<Json::UInt64>(metrics_.successful_requests.load());
        json["requests"]["failed"] = static_cast<Json::UInt64>(metrics_.failed_requests.load());
        json["requests"]["timeout"] = static_cast<Json::UInt64>(metrics_.timeout_requests.load());
        json["requests"]["retry"] = static_cast<Json::UInt64>(metrics_.retry_requests.load());
        json["requests"]["success_rate"] = metrics_.get_success_rate();

        // 性能统计
        json["performance"]["avg_wait_time_ms"] = metrics_.get_average_wait_time_ms();
        json["performance"]["max_wait_time_ms"] = static_cast<double>(metrics_.max_wait_time_us.load()) / 1000.0;
        json["performance"]["avg_execution_time_ms"] = metrics_.get_average_execution_time_ms();
        json["performance"]["max_execution_time_ms"] = static_cast<double>(metrics_.max_execution_time_us.load()) / 1000.0;

        // 健康检查
        json["health"]["checks_performed"] = static_cast<Json::UInt64>(metrics_.health_checks_performed.load());
        json["health"]["checks_failed"] = static_cast<Json::UInt64>(metrics_.health_checks_failed.load());
        json["health"]["success_rate"] = metrics_.get_health_check_success_rate();
        json["health"]["last_check_time"] = static_cast<Json::UInt64>(metrics_.last_health_check_time.load());

        // 时间戳
        json["timestamp"] = static_cast<Json::UInt64>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        return json;
    }
#else
    // 简化的字符串格式指标输出
    std::string get_metrics_string() const {
        std::ostringstream oss;
        oss << "{"
            << "\"connections\":{\"total\":" << metrics_.total_connections.load()
            << ",\"active\":" << metrics_.active_connections.load()
            << ",\"idle\":" << metrics_.idle_connections.load()
            << ",\"failed\":" << metrics_.failed_connections.load() << "},"
            << "\"requests\":{\"total\":" << metrics_.total_requests.load()
            << ",\"successful\":" << metrics_.successful_requests.load()
            << ",\"success_rate\":" << metrics_.get_success_rate() << "},"
            << "\"performance\":{\"avg_wait_time_ms\":" << metrics_.get_average_wait_time_ms()
            << ",\"avg_execution_time_ms\":" << metrics_.get_average_execution_time_ms() << "}"
            << "}";
        return oss.str();
    }
#endif

    // 重置指标
    void reset_metrics() {
        metrics_.reset();
    }

    // 记录请求开始
    void record_request_start() {
        metrics_.total_requests.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录请求成功
    void record_request_success(std::chrono::microseconds wait_time,
                               std::chrono::microseconds execution_time) {
        metrics_.successful_requests.fetch_add(1, std::memory_order_relaxed);

        // 更新等待时间统计
        auto wait_us = wait_time.count();
        metrics_.total_wait_time_us.fetch_add(wait_us, std::memory_order_relaxed);

        auto current_max = metrics_.max_wait_time_us.load();
        while (wait_us > current_max &&
               !metrics_.max_wait_time_us.compare_exchange_weak(current_max, wait_us)) {
            // CAS循环
        }

        auto current_min = metrics_.min_wait_time_us.load();
        while (wait_us < current_min &&
               !metrics_.min_wait_time_us.compare_exchange_weak(current_min, wait_us)) {
            // CAS循环
        }

        // 更新执行时间统计
        auto exec_us = execution_time.count();
        metrics_.total_execution_time_us.fetch_add(exec_us, std::memory_order_relaxed);

        current_max = metrics_.max_execution_time_us.load();
        while (exec_us > current_max &&
               !metrics_.max_execution_time_us.compare_exchange_weak(current_max, exec_us)) {
            // CAS循环
        }
    }

    // 记录请求失败
    void record_request_failure() {
        metrics_.failed_requests.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录请求超时
    void record_request_timeout() {
        metrics_.timeout_requests.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录请求重试
    void record_request_retry() {
        metrics_.retry_requests.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录连接创建
    void record_connection_created() {
        metrics_.total_connections.fetch_add(1, std::memory_order_relaxed);
        metrics_.created_connections.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录连接销毁
    void record_connection_destroyed() {
        metrics_.total_connections.fetch_sub(1, std::memory_order_relaxed);
        metrics_.destroyed_connections.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录连接激活
    void record_connection_activated() {
        metrics_.active_connections.fetch_add(1, std::memory_order_relaxed);
        metrics_.idle_connections.fetch_sub(1, std::memory_order_relaxed);
    }

    // 记录连接释放
    void record_connection_released() {
        metrics_.active_connections.fetch_sub(1, std::memory_order_relaxed);
        metrics_.idle_connections.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录连接失败
    void record_connection_failed() {
        metrics_.failed_connections.fetch_add(1, std::memory_order_relaxed);
    }

    // 记录健康检查
    void record_health_check(bool success) {
        metrics_.health_checks_performed.fetch_add(1, std::memory_order_relaxed);
        metrics_.last_health_check_time.store(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);

        if (!success) {
            metrics_.health_checks_failed.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    ConnectionPool<ConnectionType>* pool_;
    PerformanceThresholds thresholds_;
    PoolMetrics metrics_;
    AlertCallback alert_callback_;

    std::atomic<bool> monitoring_;
    std::thread monitor_thread_;
    std::unordered_map<AlertType, std::chrono::system_clock::time_point> last_alert_time_;

    void start_monitoring() {
        monitoring_ = true;
        monitor_thread_ = std::thread([this]() {
            monitor_loop();
        });
    }

    void stop_monitoring() {
        monitoring_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }

    void monitor_loop() {
        while (monitoring_) {
            try {
                check_performance_thresholds();
                std::this_thread::sleep_for(std::chrono::seconds(10)); // 每10秒检查一次
            } catch (const std::exception& e) {
                // 监控循环不应该因为异常而停止
                // 这里可以记录日志
            }
        }
    }

    void check_performance_thresholds() {
        auto now = std::chrono::system_clock::now();

        // 检查等待时间
        auto avg_wait_ms = metrics_.get_average_wait_time_ms();
        if (avg_wait_ms > thresholds_.max_wait_time.count()) {
            trigger_alert(AlertType::HIGH_WAIT_TIME,
                         "Average wait time (" + std::to_string(avg_wait_ms) + "ms) exceeds threshold",
                         now);
        }

        // 检查执行时间
        auto avg_exec_ms = metrics_.get_average_execution_time_ms();
        if (avg_exec_ms > thresholds_.max_execution_time.count()) {
            trigger_alert(AlertType::HIGH_EXECUTION_TIME,
                         "Average execution time (" + std::to_string(avg_exec_ms) + "ms) exceeds threshold",
                         now);
        }

        // 检查成功率
        auto success_rate = metrics_.get_success_rate();
        if (success_rate < thresholds_.min_success_rate) {
            trigger_alert(AlertType::LOW_SUCCESS_RATE,
                         "Success rate (" + std::to_string(success_rate * 100) + "%) below threshold",
                         now);
        }

        // 检查失败连接数
        auto failed_connections = metrics_.failed_connections.load();
        if (failed_connections > thresholds_.max_failed_connections) {
            trigger_alert(AlertType::TOO_MANY_FAILED_CONNECTIONS,
                         "Too many failed connections: " + std::to_string(failed_connections),
                         now);
        }

        // 检查连接池是否耗尽
        auto active_connections = metrics_.active_connections.load();
        auto total_connections = metrics_.total_connections.load();
        if (active_connections == total_connections && total_connections > 0) {
            // 如果所有连接都在使用中，可能接近池耗尽
            trigger_alert(AlertType::CONNECTION_POOL_EXHAUSTED,
                         "All connections are active (" + std::to_string(active_connections) + "/" +
                         std::to_string(total_connections) + ")",
                         now);
        }
    }

    void trigger_alert(AlertType type, const std::string& message,
                      std::chrono::system_clock::time_point now) {
        // 检查告警间隔
        auto it = last_alert_time_.find(type);
        if (it != last_alert_time_.end()) {
            auto time_since_last = now - it->second;
            if (time_since_last < thresholds_.alert_interval) {
                return; // 距离上次告警时间太短，跳过
            }
        }

        last_alert_time_[type] = now;

        if (alert_callback_) {
            Alert alert(type, message);
#ifdef FLOWCORO_HAS_JSON
            // 如果有JSON支持，将详细信息序列化
            auto json_metrics = get_metrics_json();
            std::ostringstream oss;
            oss << json_metrics;
            alert.details = oss.str();
#else
            alert.details = get_metrics_string();
#endif
            alert_callback_(alert);
        }
    }
};

// 便捷的监控工厂函数
template<typename ConnectionType>
std::unique_ptr<PoolMonitor<ConnectionType>> create_monitor(
    ConnectionPool<ConnectionType>* pool,
    const PerformanceThresholds& thresholds = {}) {

    return std::make_unique<PoolMonitor<ConnectionType>>(pool, thresholds);
}

} // namespace flowcoro::db
