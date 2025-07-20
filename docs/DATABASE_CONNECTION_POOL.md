# FlowCoro 数据库连接池

## 概述

FlowCoro的数据库连接池是一个高性能、协程友好的数据库连接管理系统。它提供了以下核心特性：

### 🚀 核心特性

- **协程原生支持**: 与C++20协程完全集成，支持异步数据库操作
- **连接池管理**: 自动管理数据库连接的创建、复用和销毁
- **线程安全**: 支持多线程环境下的并发访问
- **健康检查**: 自动检测和移除失效连接
- **资源控制**: 可配置的连接数限制和超时控制
- **事务支持**: 完整的数据库事务管理
- **统计监控**: 详细的连接池使用统计信息
- **驱动抽象**: 支持多种数据库后端（目前支持MySQL）

## 快速开始

### 1. 基本配置

```cpp
#include "flowcoro/db/mysql_driver.h"

using namespace flowcoro::db;

// 配置连接池
PoolConfig config;
config.min_connections = 5;        // 最小连接数
config.max_connections = 20;       // 最大连接数
config.acquire_timeout = std::chrono::seconds(10);  // 获取连接超时
config.idle_timeout = std::chrono::minutes(5);      // 连接空闲超时

// 创建MySQL连接池
auto pool = create_mysql_pool(
    "mysql://user:password@localhost:3306/database",
    config
);

// 预热连接池
co_await pool->warm_up();
```

### 2. 基本数据库操作

```cpp
// 获取连接并执行查询
Task<void> query_example() {
    // 自动获取和归还连接
    auto conn = co_await pool->acquire_connection();
    
    // 执行简单查询
    auto result = co_await conn->execute("SELECT * FROM users");
    
    if (result.success) {
        for (const auto& row : result) {
            std::cout << "User: " << row.at("name") << std::endl;
        }
    }
}

// 带参数的查询
Task<void> parameterized_query() {
    auto conn = co_await pool->acquire_connection();
    
    auto result = co_await conn->execute(
        "SELECT * FROM users WHERE age > ? AND city = ?",
        {"25", "Shanghai"}
    );
    
    std::cout << "Found " << result.size() << " users" << std::endl;
}
```

### 3. 事务管理

```cpp
// 事务示例：银行转账
Task<bool> transfer_money(int from_account, int to_account, double amount) {
    auto conn = co_await pool->acquire_connection();
    
    try {
        // 开始事务
        co_await conn->begin_transaction();
        
        // 检查余额
        auto balance_result = co_await conn->execute(
            "SELECT balance FROM accounts WHERE id = ?",
            {std::to_string(from_account)}
        );
        
        if (balance_result.empty()) {
            co_await conn->rollback();
            co_return false;
        }
        
        double current_balance = std::stod(balance_result[0].at("balance"));
        if (current_balance < amount) {
            co_await conn->rollback();
            co_return false;
        }
        
        // 扣款
        co_await conn->execute(
            "UPDATE accounts SET balance = balance - ? WHERE id = ?",
            {std::to_string(amount), std::to_string(from_account)}
        );
        
        // 入账
        co_await conn->execute(
            "UPDATE accounts SET balance = balance + ? WHERE id = ?",
            {std::to_string(amount), std::to_string(to_account)}
        );
        
        // 提交事务
        co_await conn->commit();
        co_return true;
        
    } catch (const std::exception& e) {
        co_await conn->rollback();
        throw;
    }
}
```

## 高级特性

### 1. 数据访问层模式

```cpp
class UserRepository {
public:
    UserRepository(std::shared_ptr<ConnectionPool<MySQLConnection>> pool) 
        : pool_(std::move(pool)) {}
    
    Task<std::optional<User>> find_by_id(int id) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "SELECT * FROM users WHERE id = ?",
            {std::to_string(id)}
        );
        
        if (!result.success || result.empty()) {
            co_return std::nullopt;
        }
        
        // 转换为用户对象
        User user;
        user.id = std::stoi(result[0].at("id"));
        user.name = result[0].at("name");
        user.email = result[0].at("email");
        
        co_return user;
    }
    
    Task<bool> create(const User& user) {
        auto conn = co_await pool_->acquire_connection();
        
        auto result = co_await conn->execute(
            "INSERT INTO users (name, email) VALUES (?, ?)",
            {user.name, user.email}
        );
        
        co_return result.success;
    }
    
private:
    std::shared_ptr<ConnectionPool<MySQLConnection>> pool_;
};
```

### 2. 批量操作

```cpp
Task<void> batch_insert_users(const std::vector<User>& users) {
    auto conn = co_await pool->acquire_connection();
    
    co_await conn->begin_transaction();
    
    try {
        for (const auto& user : users) {
            co_await conn->execute(
                "INSERT INTO users (name, email) VALUES (?, ?)",
                {user.name, user.email}
            );
        }
        
        co_await conn->commit();
        LOG_INFO("Successfully inserted %zu users", users.size());
        
    } catch (const std::exception& e) {
        co_await conn->rollback();
        LOG_ERROR("Batch insert failed: %s", e.what());
        throw;
    }
}
```

### 3. 并发控制

```cpp
// 并发安全的计数器更新
Task<void> increment_counter(const std::string& counter_name) {
    auto conn = co_await pool->acquire_connection();
    
    // 使用行锁确保并发安全
    auto result = co_await conn->execute(
        "UPDATE counters SET value = value + 1 WHERE name = ?",
        {counter_name}
    );
    
    if (result.affected_rows == 0) {
        // 计数器不存在，创建新的
        co_await conn->execute(
            "INSERT INTO counters (name, value) VALUES (?, 1) ON DUPLICATE KEY UPDATE value = value + 1",
            {counter_name}
        );
    }
}
```

## 性能优化

### 1. 连接池配置调优

```cpp
PoolConfig optimize_for_high_load() {
    PoolConfig config;
    
    // 高并发场景
    config.min_connections = 10;      // 保持足够的最小连接
    config.max_connections = 100;     // 允许更多峰值连接
    config.acquire_timeout = std::chrono::seconds(5);   // 较短超时
    config.idle_timeout = std::chrono::minutes(2);      // 较短空闲超时
    config.ping_interval = std::chrono::seconds(30);    // 频繁健康检查
    config.validate_on_acquire = false;  // 禁用获取时验证以提高性能
    config.validate_on_return = false;   // 禁用归还时验证
    
    return config;
}

PoolConfig optimize_for_low_load() {
    PoolConfig config;
    
    // 低并发场景
    config.min_connections = 2;       // 最少连接数
    config.max_connections = 10;      // 较少最大连接
    config.acquire_timeout = std::chrono::seconds(30);  // 较长超时
    config.idle_timeout = std::chrono::minutes(10);     // 较长空闲保持
    config.ping_interval = std::chrono::minutes(5);     // 较长健康检查间隔
    config.validate_on_acquire = true;   // 启用验证确保可靠性
    config.validate_on_return = true;
    
    return config;
}
```

### 2. 监控和诊断

```cpp
Task<void> monitor_pool_health() {
    while (!shutdown_flag) {
        auto stats = pool->get_stats();
        
        LOG_INFO("Connection Pool Stats:");
        LOG_INFO("  Total connections: %zu", stats.total_connections.load());
        LOG_INFO("  Active connections: %zu", stats.active_connections.load());
        LOG_INFO("  Idle connections: %zu", stats.idle_connections.load());
        LOG_INFO("  Success rate: %.2f%%", stats.get_success_rate() * 100);
        LOG_INFO("  Average wait time: %.2fms", stats.get_average_wait_time());
        
        // 性能告警
        if (stats.get_success_rate() < 0.95) {
            LOG_WARNING("Low success rate detected: %.2f%%", 
                       stats.get_success_rate() * 100);
        }
        
        if (stats.get_average_wait_time() > 100) {
            LOG_WARNING("High average wait time: %.2fms", 
                       stats.get_average_wait_time());
        }
        
        co_await sleep_for(std::chrono::seconds(10));
    }
}
```

## 错误处理

### 1. 连接失败处理

```cpp
Task<void> robust_query() {
    const int max_retries = 3;
    int attempt = 0;
    
    while (attempt < max_retries) {
        try {
            auto conn = co_await pool->acquire_connection();
            auto result = co_await conn->execute("SELECT NOW()");
            
            if (result.success) {
                LOG_INFO("Query successful on attempt %d", attempt + 1);
                co_return;
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Query attempt %d failed: %s", attempt + 1, e.what());
        }
        
        attempt++;
        if (attempt < max_retries) {
            // 指数退避重试
            auto delay = std::chrono::milliseconds(100 * (1 << attempt));
            co_await sleep_for(delay);
        }
    }
    
    throw std::runtime_error("Query failed after " + std::to_string(max_retries) + " attempts");
}
```

### 2. 连接池耗尽处理

```cpp
Task<std::optional<QueryResult>> query_with_timeout() {
    try {
        auto conn = co_await pool->acquire_connection();
        co_return co_await conn->execute("SELECT * FROM large_table LIMIT 1000");
        
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("timeout") != std::string::npos) {
            LOG_WARNING("Connection acquisition timeout, pool may be overloaded");
            co_return std::nullopt;
        }
        throw;
    }
}
```

## 最佳实践

### 1. 资源管理
- 总是使用RAII模式管理连接（通过ConnectionGuard）
- 避免长时间持有连接
- 在事务中尽量减少操作时间

### 2. 性能优化
- 根据应用负载调整连接池配置
- 使用批量操作减少往返次数
- 避免在循环中频繁获取/释放连接

### 3. 错误处理
- 实现适当的重试机制
- 监控连接池健康状态
- 处理网络分区和数据库重启场景

### 4. 安全考虑
- 使用参数化查询防止SQL注入
- 配置适当的连接超时
- 确保敏感信息不在日志中泄露

## 编译和部署

### 依赖要求
- C++20 编译器 (GCC 10+, Clang 12+, MSVC 19.29+)
- MySQL客户端开发库 (libmysqlclient-dev)
- CMake 3.16+

### 编译选项
```bash
# 启用数据库支持
cmake -DFLOWCORO_ENABLE_DATABASE=ON ..

# 如果MySQL安装在非标准位置
cmake -DMYSQL_INCLUDE_DIR=/opt/mysql/include \
      -DMYSQL_LIBRARY=/opt/mysql/lib/libmysqlclient.so \
      -DFLOWCORO_ENABLE_DATABASE=ON ..
```

### 运行时配置
```cpp
// 连接字符串格式
"mysql://username:password@host:port/database?charset=utf8mb4&connect_timeout=10"

// 支持的参数
// - charset: 字符集 (默认: utf8mb4)
// - connect_timeout: 连接超时秒数 (默认: 10)
// - read_timeout: 读取超时秒数 (默认: 30)
// - write_timeout: 写入超时秒数 (默认: 30)
// - auto_reconnect: 是否自动重连 (默认: true)
```

## 故障排除

### 常见问题

1. **连接超时**
   - 检查网络连通性
   - 增加连接超时时间
   - 检查数据库服务器负载

2. **连接池耗尽**
   - 增加最大连接数
   - 检查是否有连接泄露
   - 优化查询性能

3. **事务死锁**
   - 实现死锁检测和重试
   - 优化事务逻辑，减少锁持有时间
   - 统一访问顺序避免循环等待

4. **内存泄露**
   - 确保正确释放查询结果
   - 检查异常处理路径
   - 使用内存分析工具检测

## 扩展开发

### 添加新的数据库驱动

```cpp
// 实现IConnection接口
class PostgreSQLConnection : public IConnection {
    // 实现所有虚函数...
};

// 实现IDriver接口
class PostgreSQLDriver : public IDriver<PostgreSQLConnection> {
    // 实现所有虚函数...
};

// 创建工厂函数
inline std::unique_ptr<ConnectionPool<PostgreSQLConnection>> 
create_postgresql_pool(const std::string& connection_string, 
                      const PoolConfig& config = {}) {
    auto driver = std::make_unique<PostgreSQLDriver>();
    return std::make_unique<ConnectionPool<PostgreSQLConnection>>(
        std::move(driver), connection_string, config);
}
```

这个数据库连接池实现为FlowCoro项目提供了企业级的数据库访问能力，结合协程的异步特性和无锁编程的高性能，为现代C++应用提供了强大的数据持久化解决方案。
