#pragma once

#include "connection_pool.h"
#include <memory>
#include <functional>
#include <exception>

namespace flowcoro::db {

// 
enum class TransactionState {
    NOT_STARTED,
    ACTIVE,
    COMMITTED,
    ROLLED_BACK,
    FAILED
};

// 
enum class IsolationLevel {
    READ_UNCOMMITTED,
    READ_COMMITTED,
    REPEATABLE_READ,
    SERIALIZABLE
};

// 
struct TransactionOptions {
    std::chrono::milliseconds timeout{30000}; // 30
    bool auto_rollback_on_error{true}; // 
    int max_retries{3}; // 
    std::chrono::milliseconds retry_delay{100}; // 
    IsolationLevel isolation_level{IsolationLevel::READ_COMMITTED}; // 
};

//  - RAII
template<typename ConnectionType>
class Transaction {
public:
    //  - Taskpromise_type
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
        // RAII
        if (state_ == TransactionState::ACTIVE) {
            // 
            try {
                sync_rollback();
            } catch (...) {
                // 
            }
        }
    }

    // 
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    Transaction(Transaction&&) = default;
    Transaction& operator=(Transaction&&) = default;

    // 
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

    // 
    Task<void> commit() {
        if (state_ != TransactionState::ACTIVE) {
            LOG_ERROR("Transaction not active for commit");
            co_return;
        }

        check_timeout();

        bool should_auto_rollback = false;

        try {
            // commit
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

        // try-catch
        if (should_auto_rollback) {
            co_await auto_rollback();
        }
    }

private:
    // 
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

    // 
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

    // SQL - 
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

    // 
    TransactionState get_state() const { return state_; }

    // 
    bool is_active() const { return state_ == TransactionState::ACTIVE; }

    // 
    std::chrono::milliseconds get_duration() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time_);
    }

    // ()
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

    //  - 
    void sync_rollback() {
        // 
        // 
        try {
            if (connection_->is_valid()) {
                // 
                // 
            }
        } catch (...) {
            // 
        }
    }
};

// 
template<typename ConnectionType>
class TransactionManager {
public:
    TransactionManager(std::shared_ptr<ConnectionPool<ConnectionType>> pool)
        : pool_(std::move(pool)) {}

    // 
    Task<Transaction<ConnectionType>> begin_transaction(
        const TransactionOptions& options = {}) {

        auto connection = co_await pool_->acquire_connection();
        Transaction<ConnectionType> tx(std::move(connection), options);
        co_await tx.begin();
        co_return std::move(tx);
    }

    //  - 
    template<typename Func>
    auto execute_transaction(Func&& func,
                           const TransactionOptions& options = {})
        -> Task<decltype(func(std::declval<Transaction<ConnectionType>&>()).get())> {

        int retry_count = 0;
        decltype(func(std::declval<Transaction<ConnectionType>&>()).get()) result;
        while (retry_count <= options.max_retries) {
            try {
                auto tx = co_await begin_transaction(options);

                result = co_await func(tx); // 

                // 
                co_await tx.commit();

                co_return result;

            } catch (const std::exception& e) {
                if (retry_count >= options.max_retries) {
                    LOG_ERROR("Transaction retry limit exceeded: %s", e.what());
                    // 
                    decltype(result) failed_result{};
                    co_return failed_result;
                }

                ++retry_count;
                LOG_WARN("Transaction failed, retrying %d/%d: %s", retry_count, options.max_retries, e.what());

                // 
                if (options.retry_delay.count() > 0) {
                    // sleepco_await
                    std::this_thread::sleep_for(options.retry_delay);
                }

                // 
                continue;
            }
        }

        // 
        throw std::runtime_error("Unexpected error in transaction execution");
    }

    // 
    const PoolStats& get_stats() const {
        return pool_->get_stats();
    }

private:
    std::shared_ptr<ConnectionPool<ConnectionType>> pool_;

    Task<void> sleep_for(std::chrono::milliseconds duration) {
        // sleep
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

// 
#define FLOWCORO_TRANSACTION(manager, options, ...) \
    co_await manager.execute_transaction([&](auto& tx) -> Task<void> { \
        __VA_ARGS__ \
        co_return; \
    }, options)

// 
namespace tx {

// 
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

// 
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

// 
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
                // 
                auto version_result = co_await tx.execute(
                    "SELECT version FROM " + lock_table + " WHERE key = ? FOR UPDATE",
                    lock_key);

                if (!version_result.success || version_result.empty()) {
                    throw std::runtime_error("Lock key not found");
                }

                int64_t old_version = std::stoll(version_result[0].at("version"));

                // 
                auto result = co_await func(tx);

                // 
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
                // 
                co_await manager.execute_transaction([retry](auto& tx) -> Task<void> {
                    // 
                    std::this_thread::sleep_for(std::chrono::milliseconds(10 + retry * 5));
                    co_return;
                });
                continue;
            }
            throw; // 
        }
    }

    throw std::runtime_error("Optimistic lock retry limit exceeded");
}

} // namespace tx

} // namespace flowcoro::db
