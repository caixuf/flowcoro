#include <iostream>
#include <vector>
#include <chrono>
#include "flowcoro/db/mysql_driver.h"

using namespace flowcoro;
using namespace flowcoro::db;

// 示例用户数据结构
struct User {
    int id;
    std::string name;
    std::string email;
    std::chrono::system_clock::time_point created_at;
};

// 数据访问层示例
class UserRepository {
public:
    UserRepository(std::shared_ptr<ConnectionPool<MySQLConnection>> pool) 
        : pool_(std::move(pool)) {}
    
    // 创建用户
    Task<bool> create_user(const User& user) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "INSERT INTO users (name, email, created_at) VALUES (?, ?, NOW())",
            {user.name, user.email}
        );
        
        co_return result.success;
    }
    
    // 根据ID查找用户
    Task<std::optional<User>> find_user_by_id(int user_id) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "SELECT id, name, email, created_at FROM users WHERE id = ?",
            {std::to_string(user_id)}
        );
        
        if (!result.success || result.empty()) {
            co_return std::nullopt;
        }
        
        const auto& row = result[0];
        User user;
        user.id = std::stoi(row.at("id"));
        user.name = row.at("name");
        user.email = row.at("email");
        // 注意：这里简化了时间戳解析
        
        co_return user;
    }
    
    // 查找所有用户
    Task<std::vector<User>> find_all_users() {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute("SELECT id, name, email, created_at FROM users ORDER BY id");
        
        std::vector<User> users;
        if (!result.success) {
            co_return users;
        }
        
        for (const auto& row : result) {
            User user;
            user.id = std::stoi(row.at("id"));
            user.name = row.at("name");
            user.email = row.at("email");
            users.push_back(std::move(user));
        }
        
        co_return users;
    }
    
    // 更新用户
    Task<bool> update_user(const User& user) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "UPDATE users SET name = ?, email = ? WHERE id = ?",
            {user.name, user.email, std::to_string(user.id)}
        );
        
        co_return result.success && result.affected_rows > 0;
    }
    
    // 删除用户
    Task<bool> delete_user(int user_id) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "DELETE FROM users WHERE id = ?",
            {std::to_string(user_id)}
        );
        
        co_return result.success && result.affected_rows > 0;
    }
    
    // 事务示例：转账操作
    Task<bool> transfer_points(int from_user_id, int to_user_id, int points) {
        auto conn = co_await pool_->acquire_connection();
        
        try {
            // 开始事务
            co_await conn->begin_transaction();
            
            // 检查发送方余额
            auto result1 = co_await conn->execute(
                "SELECT points FROM users WHERE id = ?",
                {std::to_string(from_user_id)}
            );
            
            if (!result1.success || result1.empty()) {
                co_await conn->rollback();
                co_return false;
            }
            
            int current_points = std::stoi(result1[0].at("points"));
            if (current_points < points) {
                co_await conn->rollback();
                co_return false;
            }
            
            // 扣除发送方积分
            auto result2 = co_await conn->execute(
                "UPDATE users SET points = points - ? WHERE id = ?",
                {std::to_string(points), std::to_string(from_user_id)}
            );
            
            if (!result2.success) {
                co_await conn->rollback();
                co_return false;
            }
            
            // 增加接收方积分
            auto result3 = co_await conn->execute(
                "UPDATE users SET points = points + ? WHERE id = ?",
                {std::to_string(points), std::to_string(to_user_id)}
            );
            
            if (!result3.success) {
                co_await conn->rollback();
                co_return false;
            }
            
            // 提交事务
            co_await conn->commit();
            co_return true;
            
        } catch (const std::exception& e) {
            try {
                co_await conn->rollback();
            } catch (...) {
                // 回滚失败，记录日志
                LOG_ERROR("Failed to rollback transaction: %s", e.what());
            }
            co_return false;
        }
    }

private:
    std::shared_ptr<ConnectionPool<MySQLConnection>> pool_;
};

// 并发测试示例
Task<void> concurrent_user_operations() {
    // 创建连接池
    PoolConfig config;
    config.min_connections = 5;
    config.max_connections = 20;
    config.acquire_timeout = std::chrono::seconds(10);
    
    auto pool = create_mysql_pool(
        "mysql://user:password@localhost:3306/test_db?charset=utf8mb4&connect_timeout=10",
        config
    );
    
    // 预热连接池
    co_await pool->warm_up();
    
    UserRepository repo(pool);
    
    // 创建多个协程并发执行数据库操作
    std::vector<CoroTask> tasks;
    
    // 创建用户的协程
    for (int i = 0; i < 10; ++i) {
        tasks.emplace_back([&repo, i]() -> CoroTask {
            User user;
            user.name = "User" + std::to_string(i);
            user.email = "user" + std::to_string(i) + "@example.com";
            
            bool success = co_await repo.create_user(user);
            if (success) {
                LOG_INFO("Created user: %s", user.name.c_str());
            } else {
                LOG_ERROR("Failed to create user: %s", user.name.c_str());
            }
        }());
    }
    
    // 查询用户的协程
    for (int i = 0; i < 5; ++i) {
        tasks.emplace_back([&repo, i]() -> CoroTask {
            auto user = co_await repo.find_user_by_id(i + 1);
            if (user.has_value()) {
                LOG_INFO("Found user: %s (%s)", user->name.c_str(), user->email.c_str());
            } else {
                LOG_WARNING("User with ID %d not found", i + 1);
            }
        }());
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        co_await task;
    }
    
    // 打印连接池统计信息
    auto stats = pool->get_stats();
    LOG_INFO("Connection pool stats:");
    LOG_INFO("  Total connections: %zu", stats.total_connections.load());
    LOG_INFO("  Active connections: %zu", stats.active_connections.load());
    LOG_INFO("  Idle connections: %zu", stats.idle_connections.load());
    LOG_INFO("  Total requests: %zu", stats.total_requests.load());
    LOG_INFO("  Success rate: %.2f%%", stats.get_success_rate() * 100);
    LOG_INFO("  Average wait time: %.2fms", stats.get_average_wait_time());
}

// 压力测试示例
Task<void> stress_test() {
    LOG_INFO("Starting database connection pool stress test...");
    
    PoolConfig config;
    config.min_connections = 10;
    config.max_connections = 50;
    config.acquire_timeout = std::chrono::seconds(5);
    
    auto pool = create_mysql_pool(
        "mysql://user:password@localhost:3306/test_db",
        config
    );
    
    co_await pool->warm_up();
    UserRepository repo(pool);
    
    const int num_operations = 1000;
    const int concurrent_tasks = 50;
    
    auto start_time = std::chrono::steady_clock::now();
    
    std::vector<CoroTask> tasks;
    
    for (int i = 0; i < concurrent_tasks; ++i) {
        tasks.emplace_back([&repo, num_operations, i]() -> CoroTask {
            for (int j = 0; j < num_operations / concurrent_tasks; ++j) {
                try {
                    // 随机选择操作类型
                    int operation = (i * num_operations + j) % 4;
                    
                    switch (operation) {
                        case 0: {
                            // 创建用户
                            User user;
                            user.name = "StressUser" + std::to_string(i * 1000 + j);
                            user.email = user.name + "@stress.test";
                            co_await repo.create_user(user);
                            break;
                        }
                        case 1: {
                            // 查询用户
                            co_await repo.find_user_by_id(j % 100 + 1);
                            break;
                        }
                        case 2: {
                            // 查询所有用户
                            co_await repo.find_all_users();
                            break;
                        }
                        case 3: {
                            // 更新用户
                            User user;
                            user.id = j % 100 + 1;
                            user.name = "UpdatedUser" + std::to_string(j);
                            user.email = user.name + "@updated.test";
                            co_await repo.update_user(user);
                            break;
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR("Operation failed: %s", e.what());
                }
            }
        }());
    }
    
    // 等待所有任务完成
    for (auto& task : tasks) {
        co_await task;
    }
    
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    auto stats = pool->get_stats();
    
    LOG_INFO("Stress test completed in %lldms", duration.count());
    LOG_INFO("Total operations: %d", num_operations);
    LOG_INFO("Operations per second: %.2f", 
             static_cast<double>(num_operations) / duration.count() * 1000);
    LOG_INFO("Connection pool final stats:");
    LOG_INFO("  Success rate: %.2f%%", stats.get_success_rate() * 100);
    LOG_INFO("  Failed requests: %zu", stats.failed_requests.load());
    LOG_INFO("  Timeout requests: %zu", stats.timeout_requests.load());
    LOG_INFO("  Average wait time: %.2fms", stats.get_average_wait_time());
    LOG_INFO("  Max wait time: %llums", stats.max_wait_time_ms.load());
}

// 主函数示例
CoroTask main_coro() {
    try {
        LOG_INFO("Starting database connection pool examples...");
        
        // 运行并发操作示例
        co_await concurrent_user_operations();
        
        // 运行压力测试
        co_await stress_test();
        
        LOG_INFO("All examples completed successfully!");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Example failed: %s", e.what());
    }
}

int main() {
    try {
        auto task = main_coro();
        task.resume();
        
        // 等待协程完成
        while (!task.done()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
