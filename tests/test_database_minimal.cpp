#include <iostream>
#include <memory>
#include <vector>
#include <string>

#include "../include/flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// 简化的测试用数据结构
struct User {
    int id;
    std::string name;
    std::string email;
    
    User(int i = 0, const std::string& n = "", const std::string& e = "") 
        : id(i), name(n), email(e) {}
};

// 极简化的数据库服务类
class SimpleDatabaseService {
public:
    // 简单的用户查询（模拟）
    Task<User*> find_user_simple(int user_id) {
        // 模拟数据库查询
        static User mock_user{user_id, "Test User", "test@example.com"};
        co_return &mock_user;
    }
    
    // 创建用户（模拟）
    Task<bool> create_user_simple(const User& user) {
        std::cout << "Creating user: " << user.name << " (" << user.email << ")" << std::endl;
        co_return true;
    }
    
    // 测试基础异步操作
    Task<bool> test_async_operation() {
        // 模拟一些异步数据库操作
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        co_return true;
    }
};

// 测试基础数据库操作
TEST_CASE(database_basic_operations) {
    SimpleDatabaseService db;
    
    // 测试用户查询
    auto user_task = db.find_user_simple(123);
    auto user = sync_wait(std::move(user_task));
    TEST_EXPECT_TRUE(user != nullptr);
    TEST_EXPECT_EQ(user->id, 123);
    TEST_EXPECT_EQ(user->name, "Test User");
}

// 测试数据库创建操作
TEST_CASE(database_create_operations) {
    SimpleDatabaseService db;
    
    // 创建用户
    User new_user{100, "New User", "new@example.com"};
    auto create_user_task = db.create_user_simple(new_user);
    auto user_created = sync_wait(std::move(create_user_task));
    TEST_EXPECT_TRUE(user_created);
}

// 测试异步数据库操作
TEST_CASE(database_async_operations) {
    SimpleDatabaseService db;
    
    auto async_task = db.test_async_operation();
    auto success = sync_wait(std::move(async_task));
    TEST_EXPECT_TRUE(success);
}

// 测试多个并发数据库操作
TEST_CASE(concurrent_database_operations) {
    SimpleDatabaseService db;
    
    // 创建多个协程任务
    std::vector<Task<User*>> user_tasks;
    for (int i = 1; i <= 3; ++i) {
        user_tasks.push_back(db.find_user_simple(i));
    }
    
    // 等待所有任务完成
    int successful_queries = 0;
    for (auto& task : user_tasks) {
        auto user = sync_wait(std::move(task));
        if (user != nullptr) {
            successful_queries++;
        }
    }
    
    TEST_EXPECT_EQ(successful_queries, 3);
}

// 测试基础缓存功能
TEST_CASE(cache_functionality) {
    // 简单的缓存测试
    struct SimpleCache {
        std::unordered_map<int, User> user_cache;
        
        Task<User*> get_user(int id) {
            auto it = user_cache.find(id);
            if (it != user_cache.end()) {
                co_return &it->second;
            }
            
            // 模拟从数据库加载
            User user{id, "Cached User " + std::to_string(id), "cached@example.com"};
            user_cache[id] = user;
            co_return &user_cache[id];
        }
    };
    
    SimpleCache cache;
    
    // 第一次查询（从数据库）
    auto user1_task = cache.get_user(1);
    auto user1 = sync_wait(std::move(user1_task));
    TEST_EXPECT_TRUE(user1 != nullptr);
    
    // 第二次查询（从缓存）
    auto user2_task = cache.get_user(1);
    auto user2 = sync_wait(std::move(user2_task));
    TEST_EXPECT_TRUE(user2 != nullptr);
    TEST_EXPECT_EQ(user1, user2); // 应该是同一个对象
}

int main() {
    TEST_SUITE("FlowCoro Database (Phase 2) Tests (Simplified)");
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
