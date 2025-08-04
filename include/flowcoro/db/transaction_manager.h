#pragma once

#include "connection_pool.h"
#include <memory>
#include <functional>
#include <exception>

namespace flowcoro::db {

// 事务状态
enum class TransactionState {
    NOT_STARTED,
    ACTIVE,
    COMMITTED,
    ROLLED_BACK,
    FAILED
};

// 事务隔离级别
enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

// 事务选项
struct TransactionOptions {
    std::chrono::milliseconds timeout{30000}; // 30秒超时
    bool auto_rollback_on_error{true}; // 错误时自动回滚
    int max_retries{3}; // 最大重试次数
    std::chrono::milliseconds retry_delay{100}; // 重试延迟
    IsolationLevel isolation_level{IsolationLevel::READ_COMMITTED}; // 默认隔离级别
};

// 事务管理器 - 提供RAII事务管理
template<typename ConnectionType>
class Transaction {
public:
    // 默认构造函数 - 用于Task的promise_type
    Transaction()
        : connection_(ConnectionGuard<ConnectionType>{})
        , options_({})
        , state_(TransactionState::NOT_STARTED)
        , start_time_(std::chrono::steady_clock::now()) {}

    Transaction(ConnectionGuard<ConnectionType>&& connection,
               const TransactionOptions& options = {})
        : connection_(std::move(connection))
        , options_(options)
        , state_(TransactionState::NOT_STARTED)
        , start_time_(std::chrono::steady_clock::now()) {}

    ~Transaction() {
        // RAII自动清理
        if (state_ == TransactionState::ACTIVE) {
            // 在析构函数中不能使用协程，所以需要同步回滚
            try {
                sync_rollback();
            } catch (...) {
                // 忽略析构函数中的异常
            }
        }
    }

    // 禁止拷贝，允许移动
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = default;
    Transaction& operator=(Transaction&&) = default;

    // 开始事务
    Task<void> begin() {
        if (state_ != TransactionState::NOT_STARTED) {
            throw std::runtime_error("Transaction already started");
        }

        try {
            co_await connection_->begin_transaction();
            state_ = TransactionState::ACTIVE;
            start_time_ = std::chrono::steady_clock::now();
        } catch (const std::exception& e) {
            state_ = TransactionState::FAILED;
            throw;
        }
    }

    // 提交事务
    Task<void> commit() {
        if (state_ != TransactionState::ACTIVE) {
            LOG_ERROR("Transaction not active for commit");
            co_return;
        }

        check_timeout();

        bool should_auto_rollback = false;

        try {
            // 处理可能抛出异常的commit调用
            auto commit_result = co_await connection_->commit();
            if (commit_result.success) {
                state_ = TransactionState::COMMITTED;
            } else {
                state_ = TransactionState::FAILED;
                LOG_ERROR("Transaction commit failed: %s", commit_result.error.c_str());
                should_auto_rollback = options_.auto_rollback_on_error;
            }
        } catch (const std::exception& e) {
            state_ = TransactionState::FAILED;
            LOG_ERROR("Transaction commit threw exception: %s", e.what());
            should_auto_rollback = options_.auto_rollback_on_error;
        }

        // 在try-catch之外处理回滚
        if (should_auto_rollback) {
            co_await auto_rollback();
        }
    }

private:
    // 自动回滚辅助方法
    Task<void> auto_rollback() {
        try {
            auto rollback_result = co_await rollback();
            if (!rollback_result.success) {
                LOG_ERROR("Auto rollback failed: %s", rollback_result.error.c_str());
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Auto rollback threw exception: %s", e.what());
        }
    }

public:

    // 回滚事务
    Task<QueryResult> rollback() {
        if (state_ != TransactionState::ACTIVE) {
            LOG_ERROR("Transaction not active for rollback");
            QueryResult result;
            result.success = false;
            result.error = "Transaction not active";
            co_return result;
        }

        try {
            auto rollback_result = co_await connection_->rollback();
            if (rollback_result.success) {
                state_ = TransactionState::ROLLED_BACK;
            } else {
                state_ = TransactionState::FAILED;
                LOG_ERROR("Transaction rollback failed: %s", rollback_result.error.c_str());
            }
            co_return rollback_result;
        } catch (const std::exception& e) {
            state_ = TransactionState::FAILED;
            QueryResult result;
            result.success = false;
            result.error = "Transaction rollback threw exception: " + std::string(e.what());
            LOG_ERROR("Transaction rollback threw exception: %s", e.what());
            co_return result;
        }
    }

    // 执行SQL - 自动事务管理
    template<typename... Args>
    Task<QueryResult> execute(const std::string& sql, Args&&... args) {
        if (state_ != TransactionState::ACTIVE) {
            LOG_ERROR("Transaction not active for execute");
            QueryResult result;
            result.success = false;
            result.error = "Transaction not active";
            co_return result;
        }

        check_timeout();

        QueryResult result;
        if constexpr (sizeof...(args) == 0) {
            result = co_await connection_->execute(sql);
        } else {
            std::vector<std::string> params{std::forward<Args>(args)...};
            result = co_await connection_->execute(sql, params);
        }

        if (!result.success && options_.auto_rollback_on_error) {
            auto rollback_result = co_await rollback();
            if (!rollback_result.success) {
                LOG_ERROR("Auto rollback failed: %s", rollback_result.error.c_str());
            }
        }

        co_return result;
    }

    // 获取事务状态
    TransactionState get_state() const { return state_; }

    // 检查事务是否活跃
    bool is_active() const { return state_ == TransactionState::ACTIVE; }

    // 获取事务持续时间
    std::chrono::milliseconds get_duration() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_);
    }

    // 获取底层连接(谨慎使用)
    ConnectionType* get_connection() { return connection_.operator->(); }

private:
    ConnectionGuard<ConnectionType> connection_;
    TransactionOptions options_;
    TransactionState state_;
    std::chrono::steady_clock::time_point start_time_;

    void check_timeout() const {
        if (get_duration() > options_.timeout) {
            throw std::runtime_error("Transaction timeout");
        }
    }

    // 同步回滚 - 在析构函数中使用
    void sync_rollback() {
        // 这里需要实现同步版本的回滚
        // 实际项目中可能需要使用线程池执行
        try {
            if (connection_->is_valid()) {
                // 简单的同步回滚实现
                // 注意：这里可能阻塞，仅作为最后手段
            }
        } catch (...) {
            // 忽略异常
        }
    }
};

// 事务管理器工厂
template<typename ConnectionType>
class TransactionManager {
public:
    TransactionManager(std::shared_ptr<ConnectionPool<ConnectionType>> pool)
        : pool_(std::move(pool)) {}

    // 开始新事务
    Task<Transaction<ConnectionType>> begin_transaction(
        const TransactionOptions& options = {}) {

        auto connection = co_await pool_->acquire_connection();
        Transaction<ConnectionType> tx(std::move(connection), options);
        co_await tx.begin();
        co_return std::move(tx);
    }

    // 执行事务性操作 - 自动管理事务生命周期
    template<typename Func>
    auto execute_transaction(Func&& func,
                           const TransactionOptions& options = {})
        -> Task<decltype(func(std::declval<Transaction<ConnectionType>&>()).get())> {

        int retry_count = 0;
        decltype(func(std::declval<Transaction<ConnectionType>&>()).get()) result;
        while (retry_count <= options.max_retries) {
            try {
                auto tx = co_await begin_transaction(options);

                result = co_await func(tx); // 执行用户代码

                // 提交事务
                co_await tx.commit();

                co_return result;

            } catch (const std::exception& e) {
                if (retry_count >= options.max_retries) {
                    LOG_ERROR("Transaction retry limit exceeded: %s", e.what());
                    // 返回失败的结果而不是抛异常
                    decltype(result) failed_result{};
                    co_return failed_result;
                }

                ++retry_count;
                LOG_WARN("Transaction failed, retrying %d/%d: %s", retry_count, options.max_retries, e.what());

                // 等待一段时间后重试
                if (options.retry_delay.count() > 0) {
                    // 简化的sleep实现，不在异常处理中使用co_await
                    std::this_thread::sleep_for(options.retry_delay);
                }

                // 继续重试
                continue;
            }
        }

        // 不应该到达这里
        throw std::runtime_error("Unexpected error in transaction execution");
    }

    // 获取连接池统计
    const PoolStats& get_stats() const {
        return pool_->get_stats();
    }

private:
    std::shared_ptr<ConnectionPool<ConnectionType>> pool_;

    Task<void> sleep_for(std::chrono::milliseconds duration) {
        // 简单的异步sleep实现
        class SleepAwaiter {
            std::chrono::milliseconds duration_;
        public:
            SleepAwaiter(std::chrono::milliseconds duration) : duration_(duration) {}

            bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h) {
                GlobalThreadPool::get().enqueue_void([duration = duration_, h]() {
                    std::this_thread::sleep_for(duration);
                    if (h && !h.done()) {
                        h.resume();
                    }
                });
            }

            void await_resume() const noexcept {}
        };

        co_await SleepAwaiter{duration};
    }
};

// 便捷的事务执行宏
#define FLOWCORO_TRANSACTION(manager, options, ...) \
    co_await manager.execute_transaction([&](auto& tx) -> Task<void> { \
        __VA_ARGS__ \
        co_return; \
    }, options)

// 异步事务操作辅助函数
namespace tx {

// 批量操作
template<typename ConnectionType>
Task<std::vector<QueryResult>> batch_execute(
    Transaction<ConnectionType>& tx,
    const std::vector<std::string>& queries) {

    std::vector<QueryResult> results;
    results.reserve(queries.size());

    for (const auto& query : queries) {
        auto result = co_await tx.execute(query);
        results.push_back(std::move(result));

        if (!results.back().success) {
            throw std::runtime_error("Batch execution failed: " + results.back().error);
        }
    }

    co_return results;
}

// 条件执行
template<typename ConnectionType, typename Predicate>
Task<QueryResult> execute_if(
    Transaction<ConnectionType>& tx,
    const std::string& condition_query,
    const std::string& action_query,
    Predicate pred) {

    auto condition_result = co_await tx.execute(condition_query);
    if (!condition_result.success) {
        throw std::runtime_error("Condition query failed: " + condition_result.error);
    }

    if (pred(condition_result)) {
        co_return co_await tx.execute(action_query);
    } else {
        QueryResult empty_result;
        empty_result.success = true;
        co_return empty_result;
    }
}

// 乐观锁重试
template<typename ConnectionType, typename Func>
auto with_optimistic_lock(
    TransactionManager<ConnectionType>& manager,
    const std::string& lock_table,
    const std::string& lock_key,
    Func&& func,
    int max_retries = 10) -> Task<decltype(func(std::declval<Transaction<ConnectionType>&>()).get())> {

    for (int retry = 0; retry < max_retries; ++retry) {
        try {
            co_return co_await manager.execute_transaction([&](auto& tx) -> Task<decltype(func(tx).get())> {
                // 读取版本号
                auto version_result = co_await tx.execute(
                    "SELECT version FROM " + lock_table + " WHERE key = ? FOR UPDATE",
                    lock_key);

                if (!version_result.success || version_result.empty()) {
                    throw std::runtime_error("Lock key not found");
                }

                int64_t old_version = std::stoll(version_result[0].at("version"));

                // 执行用户操作
                auto result = co_await func(tx);

                // 更新版本号
                auto update_result = co_await tx.execute(
                    "UPDATE " + lock_table + " SET version = version + 1 WHERE key = ? AND version = ?",
                    lock_key, std::to_string(old_version));

                if (!update_result.success || update_result.affected_rows == 0) {
                    throw std::runtime_error("Optimistic lock conflict");
                }

                co_return result;
            });

        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("lock conflict") != std::string::npos) {
                // 乐观锁冲突，重试
                co_await manager.execute_transaction([retry](auto& tx) -> Task<void> {
                    // 短暂延迟
                    std::this_thread::sleep_for(std::chrono::milliseconds(10 + retry * 5));
                    co_return;
                });
                continue;
            }
            throw; // 其他错误直接抛出
        }
    }

    throw std::runtime_error("Optimistic lock retry limit exceeded");
}

} // namespace tx

} // namespace flowcoro::db
