#pragma once

#include <memory>
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <future>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <optional>

#include <flowcoro/core.h>
#include <flowcoro/lockfree.h>

namespace flowcoro::db {

// 
template<typename Driver>
class ConnectionPool;

// 
struct ConnectionPoolConfig {
    size_t min_connections = 5; // 
    size_t max_connections = 20; // 
    std::chrono::seconds connection_timeout{30}; // 
    std::chrono::seconds idle_timeout{300}; // 
    std::chrono::seconds wait_timeout{10}; // 
    bool auto_reconnect = true; // 
    size_t max_reconnect_attempts = 3; // 
};

// 
struct QueryResult {
    bool success{false};
    std::string error;
    std::vector<std::unordered_map<std::string, std::string>> rows;
    uint64_t affected_rows{0};
    uint64_t insert_id{0};

    // 
    bool empty() const { return rows.empty(); }
    size_t size() const { return rows.size(); }

    const std::unordered_map<std::string, std::string>& operator[](size_t index) const {
        return rows[index];
    }

    // 
    auto begin() const { return rows.begin(); }
    auto end() const { return rows.end(); }
};

// 
class IConnection {
public:
    virtual ~IConnection() = default;

    // SQL
    virtual Task<QueryResult> execute(const std::string& sql) = 0;
    virtual Task<QueryResult> execute(const std::string& sql, const std::vector<std::string>& params) = 0;

    // 
    virtual Task<QueryResult> begin_transaction() = 0;
    virtual Task<QueryResult> commit() = 0;
    virtual Task<QueryResult> rollback() = 0;

    // 
    virtual bool is_valid() const = 0;
    virtual Task<bool> ping() = 0;
    virtual void close() = 0;

    // 
    virtual std::string get_error() const = 0;
    virtual uint64_t get_last_insert_id() const = 0;
    virtual uint64_t get_affected_rows() const = 0;
};

// 
template<typename ConnectionType>
class IDriver {
public:
    virtual ~IDriver() = default;

    // 
    virtual Task<std::unique_ptr<ConnectionType>> create_connection(
        const std::string& connection_string) = 0;

    // 
    virtual bool validate_connection_string(const std::string& connection_string) const = 0;

    // 
    virtual std::string get_driver_name() const = 0;
    virtual std::string get_version() const = 0;
};

// 
struct PoolConfig {
    size_t min_connections{5}; // 
    size_t max_connections{20}; // 
    std::chrono::milliseconds acquire_timeout{5000}; // 
    std::chrono::milliseconds idle_timeout{300000}; // (5)
    std::chrono::milliseconds ping_interval{60000}; // (1)
    bool validate_on_acquire{true}; // 
    bool validate_on_return{false}; // 
    size_t max_retries{3}; // 
};

//  - 
template<typename ConnectionType>
class PooledConnection {
public:
    PooledConnection(std::unique_ptr<ConnectionType> conn,
                    ConnectionPool<ConnectionType>* pool)
        : connection_(std::move(conn))
        , pool_(pool)
        , created_time_(std::chrono::steady_clock::now())
        , last_used_time_(created_time_)
        , in_use_(false) {}

    ~PooledConnection() {
        if (connection_) {
            connection_->close();
        }
    }

    // 
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;
    PooledConnection(PooledConnection&&) = default;
    PooledConnection& operator=(PooledConnection&&) = default;

    ConnectionType* get() { return connection_.get(); }
    const ConnectionType* get() const { return connection_.get(); }

    bool is_valid() const {
        return connection_ && connection_->is_valid();
    }

    Task<bool> ping() {
        if (!connection_) co_return false;
        co_return co_await connection_->ping();
    }

    void mark_used() {
        last_used_time_ = std::chrono::steady_clock::now();
        in_use_ = true;
    }

    void mark_unused() {
        last_used_time_ = std::chrono::steady_clock::now();
        in_use_ = false;
    }

    bool is_in_use() const { return in_use_; }

    auto get_idle_time() const {
        return std::chrono::steady_clock::now() - last_used_time_;
    }

    auto get_age() const {
        return std::chrono::steady_clock::now() - created_time_;
    }

private:
    std::unique_ptr<ConnectionType> connection_;
    ConnectionPool<ConnectionType>* pool_;
    std::chrono::steady_clock::time_point created_time_;
    std::chrono::steady_clock::time_point last_used_time_;
    std::atomic<bool> in_use_{false};
};

// 
struct PoolStats {
    std::atomic<size_t> total_connections{0};
    std::atomic<size_t> active_connections{0};
    std::atomic<size_t> idle_connections{0};
    std::atomic<size_t> failed_connections{0};
    std::atomic<size_t> total_requests{0};
    std::atomic<size_t> successful_requests{0};
    std::atomic<size_t> failed_requests{0};
    std::atomic<size_t> timeout_requests{0};
    std::atomic<size_t> retry_requests{0};
    std::atomic<uint64_t> total_wait_time_ms{0};
    std::atomic<uint64_t> max_wait_time_ms{0};

    double get_success_rate() const {
        auto total = total_requests.load();
        if (total == 0) return 1.0;
        return static_cast<double>(successful_requests.load()) / total;
    }

    double get_average_wait_time() const {
        auto total = total_requests.load();
        if (total == 0) return 0.0;
        return static_cast<double>(total_wait_time_ms.load()) / total;
    }
};

// RAII
template<typename ConnectionType>
class ConnectionGuard {
public:
    //  - Taskpromise_type
    ConnectionGuard() : connection_(nullptr), pool_(nullptr) {}

    ConnectionGuard(std::shared_ptr<PooledConnection<ConnectionType>> conn,
                   ConnectionPool<ConnectionType>* pool)
        : connection_(std::move(conn)), pool_(pool) {
        if (connection_) {
            connection_->mark_used();
        }
    }

    ~ConnectionGuard() {
        if (connection_ && pool_) {
            connection_->mark_unused();
            pool_->return_connection(connection_);
        }
    }

    // 
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
    ConnectionGuard(ConnectionGuard&&) = default;
    ConnectionGuard& operator=(ConnectionGuard&&) = default;

    ConnectionType* operator->() { return connection_->get(); }
    ConnectionType& operator*() { return *connection_->get(); }

    bool is_valid() const { return connection_ && connection_->is_valid(); }

private:
    std::shared_ptr<PooledConnection<ConnectionType>> connection_;
    ConnectionPool<ConnectionType>* pool_;
};

//  - 
template<typename ConnectionType>
class ConnectionPool {
public:
    using ConnectionPtr = std::shared_ptr<PooledConnection<ConnectionType>>;
    using DriverPtr = std::unique_ptr<IDriver<ConnectionType>>;

    ConnectionPool(DriverPtr driver, const std::string& connection_string,
                  const PoolConfig& config = {})
        : driver_(std::move(driver))
        , connection_string_(connection_string)
        , config_(config)
        , shutdown_(false)
        , stats_()
    {
        if (!driver_->validate_connection_string(connection_string_)) {
            throw std::invalid_argument("Invalid connection string");
        }

        LOG_INFO("Initializing connection pool with config: min=%zu, max=%zu",
                config_.min_connections, config_.max_connections);

        // 
        start_background_tasks();
    }

    ~ConnectionPool() {
        shutdown();
    }

    //  - 
    Task<ConnectionGuard<ConnectionType>> acquire_connection() {
        auto start_time = std::chrono::steady_clock::now();
        stats_.total_requests.fetch_add(1, std::memory_order_relaxed);

        // 
        if (auto conn = try_get_available_connection()) {
            auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            update_wait_time_stats(wait_time);

            stats_.successful_requests.fetch_add(1, std::memory_order_relaxed);
            co_return ConnectionGuard<ConnectionType>(std::move(conn), this);
        }

        // 
        if (can_create_new_connection()) {
            if (auto new_conn = co_await create_new_connection()) {
                auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start_time).count();
                update_wait_time_stats(wait_time);

                stats_.successful_requests.fetch_add(1, std::memory_order_relaxed);
                co_return ConnectionGuard<ConnectionType>(std::move(new_conn), this);
            }
        }

        // 
        auto conn = co_await wait_for_available_connection(start_time);
        if (conn) {
            auto wait_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count();
            update_wait_time_stats(wait_time);

            stats_.successful_requests.fetch_add(1, std::memory_order_relaxed);
            co_return ConnectionGuard<ConnectionType>(std::move(conn), this);
        }

        // 
        stats_.timeout_requests.fetch_add(1, std::memory_order_relaxed);
        stats_.failed_requests.fetch_add(1, std::memory_order_relaxed);
        throw std::runtime_error("Failed to acquire connection within timeout");
    }

    // 
    void return_connection(ConnectionPtr conn) {
        if (!conn || shutdown_.load()) return;

        std::lock_guard<std::mutex> lock(pool_mutex_);

        // 
        if (config_.validate_on_return && !conn->is_valid()) {
            stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
            remove_connection_unsafe(conn);
            return;
        }

        // 
        conn->mark_unused();
        idle_connections_.push(conn);
        stats_.active_connections.fetch_sub(1, std::memory_order_relaxed);
        stats_.idle_connections.fetch_add(1, std::memory_order_relaxed);

        // 
        pool_condition_.notify_one();
    }

    // 
    PoolStats get_stats() const { return stats_; }

    // 
    const PoolConfig& get_config() const { return config_; }

    // 
    void shutdown() {
        if (shutdown_.exchange(true)) return;

        LOG_INFO("Shutting down connection pool");

        // 
        stop_background_tasks();

        // 
        std::lock_guard<std::mutex> lock(pool_mutex_);
        cleanup_all_connections_unsafe();

        // 
        pool_condition_.notify_all();
    }

    // 
    Task<void> health_check() {
        co_await perform_health_check();
    }

    //  - 
    Task<void> warm_up() {
        std::vector<CoroTask> create_tasks;

        std::lock_guard<std::mutex> lock(pool_mutex_);
        size_t connections_to_create = config_.min_connections;

        for (size_t i = 0; i < connections_to_create; ++i) {
            create_tasks.emplace_back(create_and_add_connection());
        }

        // 
        for (auto& task : create_tasks) {
            co_await task;
        }

        LOG_INFO("Connection pool warmed up with %zu connections", connections_to_create);
    }

private:
    DriverPtr driver_;
    std::string connection_string_;
    PoolConfig config_;
    std::atomic<bool> shutdown_;
    PoolStats stats_;

    // 
    mutable std::mutex pool_mutex_;
    std::condition_variable pool_condition_;
    std::queue<ConnectionPtr> idle_connections_;
    std::unordered_set<ConnectionPtr> all_connections_;

    // 
    std::vector<std::thread> background_threads_;

    // 
    ConnectionPtr try_get_available_connection() {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        while (!idle_connections_.empty()) {
            auto conn = idle_connections_.front();
            idle_connections_.pop();

            // 
            if (config_.validate_on_acquire && !conn->is_valid()) {
                stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
                remove_connection_unsafe(conn);
                continue;
            }

            stats_.idle_connections.fetch_sub(1, std::memory_order_relaxed);
            stats_.active_connections.fetch_add(1, std::memory_order_relaxed);
            return conn;
        }

        return nullptr;
    }

    bool can_create_new_connection() const {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        return all_connections_.size() < config_.max_connections;
    }

    Task<ConnectionPtr> create_new_connection() {
        try {
            auto raw_conn = co_await driver_->create_connection(connection_string_);
            if (!raw_conn) {
                stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
                co_return nullptr;
            }

            auto pooled_conn = std::make_shared<PooledConnection<ConnectionType>>(
                std::move(raw_conn), this);

            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                all_connections_.insert(pooled_conn);
                stats_.total_connections.fetch_add(1, std::memory_order_relaxed);
                stats_.active_connections.fetch_add(1, std::memory_order_relaxed);
            }

            LOG_DEBUG("Created new database connection, total: %zu",
                     stats_.total_connections.load());

            co_return pooled_conn;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create database connection: %s", e.what());
            stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
            co_return nullptr;
        }
    }

    Task<ConnectionPtr> wait_for_available_connection(
        std::chrono::steady_clock::time_point start_time) {

        std::unique_lock<std::mutex> lock(pool_mutex_);

        while (!shutdown_.load()) {
            // 
            if (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();

                if (config_.validate_on_acquire && !conn->is_valid()) {
                    stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
                    remove_connection_unsafe(conn);
                    continue;
                }

                stats_.idle_connections.fetch_sub(1, std::memory_order_relaxed);
                stats_.active_connections.fetch_add(1, std::memory_order_relaxed);
                co_return conn;
            }

            // 
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - start_time;
            if (elapsed >= config_.acquire_timeout) {
                co_return nullptr;
            }

            // 
            auto remaining_time = config_.acquire_timeout - elapsed;
            if (pool_condition_.wait_for(lock, remaining_time) == std::cv_status::timeout) {
                co_return nullptr;
            }
        }

        co_return nullptr;
    }

    void remove_connection_unsafe(ConnectionPtr conn) {
        all_connections_.erase(conn);
        stats_.total_connections.fetch_sub(1, std::memory_order_relaxed);

        // 
        std::queue<ConnectionPtr> temp_queue;
        while (!idle_connections_.empty()) {
            auto idle_conn = idle_connections_.front();
            idle_connections_.pop();
            if (idle_conn != conn) {
                temp_queue.push(idle_conn);
            } else {
                stats_.idle_connections.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        idle_connections_ = std::move(temp_queue);
    }

    void cleanup_all_connections_unsafe() {
        // 
        while (!idle_connections_.empty()) {
            idle_connections_.pop();
        }

        // 
        for (auto& conn : all_connections_) {
            if (conn && conn->get()) {
                conn->get()->close();
            }
        }

        all_connections_.clear();
        stats_.total_connections.store(0);
        stats_.active_connections.store(0);
        stats_.idle_connections.store(0);
    }

    void update_wait_time_stats(uint64_t wait_time_ms) {
        stats_.total_wait_time_ms.fetch_add(wait_time_ms, std::memory_order_relaxed);

        // 
        uint64_t current_max = stats_.max_wait_time_ms.load();
        while (wait_time_ms > current_max &&
               !stats_.max_wait_time_ms.compare_exchange_weak(current_max, wait_time_ms)) {
            // CAS
        }
    }

    CoroTask create_and_add_connection() {
        try {
            auto raw_conn = co_await driver_->create_connection(connection_string_);
            if (!raw_conn) {
                stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
                co_return;
            }

            auto pooled_conn = std::make_shared<PooledConnection<ConnectionType>>(
                std::move(raw_conn), this);

            {
                std::lock_guard<std::mutex> lock(pool_mutex_);
                all_connections_.insert(pooled_conn);
                idle_connections_.push(pooled_conn);
                stats_.total_connections.fetch_add(1, std::memory_order_relaxed);
                stats_.idle_connections.fetch_add(1, std::memory_order_relaxed);
            }

            LOG_DEBUG("Added connection to pool, total: %zu",
                     stats_.total_connections.load());
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create connection during warm-up: %s", e.what());
            stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // 
    void start_background_tasks() {
        // 
        background_threads_.emplace_back([this] {
            health_check_task();
        });

        // 
        background_threads_.emplace_back([this] {
            cleanup_task();
        });
    }

    void stop_background_tasks() {
        for (auto& thread : background_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        background_threads_.clear();
    }

    void health_check_task() {
        while (!shutdown_.load()) {
            try {
                // 
                std::this_thread::sleep_for(config_.ping_interval);
                if (shutdown_.load()) break;

                // 
                GlobalThreadPool::get().enqueue_void([this]() {
                    perform_health_check();
                });

            } catch (const std::exception& e) {
                LOG_ERROR("Health check task error: %s", e.what());
            }
        }
    }

    void cleanup_task() {
        while (!shutdown_.load()) {
            try {
                // 30
                std::this_thread::sleep_for(std::chrono::seconds(30));
                if (shutdown_.load()) break;

                cleanup_idle_connections();

            } catch (const std::exception& e) {
                LOG_ERROR("Cleanup task error: %s", e.what());
            }
        }
    }

    Task<void> perform_health_check() {
        std::vector<ConnectionPtr> connections_to_check;

        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            // 
            std::queue<ConnectionPtr> temp_queue;
            while (!idle_connections_.empty()) {
                auto conn = idle_connections_.front();
                idle_connections_.pop();
                connections_to_check.push_back(conn);
            }
        }

        std::vector<ConnectionPtr> healthy_connections;

        for (auto& conn : connections_to_check) {
            try {
                bool is_healthy = co_await conn->ping();
                if (is_healthy) {
                    healthy_connections.push_back(conn);
                } else {
                    LOG_WARN("Connection failed health check, removing from pool");
                    std::lock_guard<std::mutex> lock(pool_mutex_);
                    remove_connection_unsafe(conn);
                    stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Health check failed for connection: %s", e.what());
                std::lock_guard<std::mutex> lock(pool_mutex_);
                remove_connection_unsafe(conn);
                stats_.failed_connections.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // 
        {
            std::lock_guard<std::mutex> lock(pool_mutex_);
            for (auto& conn : healthy_connections) {
                idle_connections_.push(conn);
            }
        }
    }

    void cleanup_idle_connections() {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        std::queue<ConnectionPtr> active_connections;
        size_t removed_count = 0;

        while (!idle_connections_.empty()) {
            auto conn = idle_connections_.front();
            idle_connections_.pop();

            // 
            if (conn->get_idle_time() > config_.idle_timeout) {
                // 
                if (all_connections_.size() - removed_count > config_.min_connections) {
                    remove_connection_unsafe(conn);
                    removed_count++;
                    LOG_DEBUG("Removed idle connection, remaining: %zu",
                             all_connections_.size());
                } else {
                    active_connections.push(conn);
                }
            } else {
                active_connections.push(conn);
            }
        }

        idle_connections_ = std::move(active_connections);

        if (removed_count > 0) {
            LOG_INFO("Cleaned up %zu idle connections", removed_count);
        }
    }
};

} // namespace flowcoro::db