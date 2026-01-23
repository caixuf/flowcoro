#include <iostream>
#include <string>
#include <stdexcept>
#include <memory>

#include "flowcoro.hpp"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;

// 测试基本异常抛出
Task<int> failing_task() {
    throw std::runtime_error("Test exception");
    co_return 42; // 不会执行到这里
}

// 测试异常传播到等待的协程
Task<int> caller_task() {
    auto result = co_await failing_task();
    co_return result;
}

// 测试正常任务
Task<int> normal_task() {
    co_return 100;
}

// 测试Task<void>的异常
Task<void> failing_void_task() {
    throw std::runtime_error("Void task exception");
    co_return;
}

// 测试Task<void>的异常传播
Task<void> caller_void_task() {
    co_await failing_void_task();
    co_return;
}

// 测试Task<unique_ptr<T>>的异常
Task<std::unique_ptr<int>> failing_unique_ptr_task() {
    throw std::runtime_error("Unique ptr task exception");
    co_return std::make_unique<int>(42);
}

// 测试Task<unique_ptr<T>>的异常传播
Task<std::unique_ptr<int>> caller_unique_ptr_task() {
    auto result = co_await failing_unique_ptr_task();
    co_return result;
}

// 测试不同类型的异常
Task<int> custom_exception_task() {
    throw std::logic_error("Logic error exception");
    co_return 42;
}

// 测试1: 基本异常捕获
TEST_CASE(basic_exception) {
    auto task = failing_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Test exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试2: 异常通过co_await传播
TEST_CASE(exception_propagation_through_await) {
    auto task = caller_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Test exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试3: try_get()不抛异常
TEST_CASE(try_get_returns_nullopt) {
    auto task = failing_task();
    
    // 先触发执行
    try {
        task.get();
    } catch (...) {
        // 预期会抛出异常
    }
    
    // try_get应该返回nullopt而不是抛异常
    auto result = task.try_get();
    TEST_EXPECT_FALSE(result.has_value());
}

// 测试4: get_error_message()获取错误信息
TEST_CASE(get_error_message) {
    auto task = failing_task();
    
    // 先触发执行
    try {
        task.get();
    } catch (...) {
        // 预期会抛出异常
    }
    
    auto msg = task.get_error_message();
    TEST_EXPECT_TRUE(msg.has_value());
    TEST_EXPECT_TRUE(*msg == "Test exception");
}

// 测试5: 正常任务不应该有错误
TEST_CASE(normal_task_no_exception) {
    auto task = normal_task();
    
    auto result = task.get();
    TEST_EXPECT_EQ(result, 100);
    
    auto msg = task.get_error_message();
    TEST_EXPECT_FALSE(msg.has_value());
}

// 测试6: Task<void>的异常处理
TEST_CASE(void_task_exception) {
    auto task = failing_void_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Void task exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试7: Task<void>的异常传播
TEST_CASE(void_task_exception_propagation) {
    auto task = caller_void_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Void task exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试8: Task<void>的try_get()
TEST_CASE(void_task_try_get) {
    auto task = failing_void_task();
    
    // 先触发执行
    try {
        task.get();
    } catch (...) {
    }
    
    auto result = task.try_get();
    TEST_EXPECT_FALSE(result);
}

// 测试9: Task<unique_ptr<T>>的异常处理
TEST_CASE(unique_ptr_task_exception) {
    auto task = failing_unique_ptr_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Unique ptr task exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试10: Task<unique_ptr<T>>的异常传播
TEST_CASE(unique_ptr_task_exception_propagation) {
    auto task = caller_unique_ptr_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::runtime_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Unique ptr task exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 测试11: Task<unique_ptr<T>>的try_get()
TEST_CASE(unique_ptr_task_try_get) {
    auto task = failing_unique_ptr_task();
    
    // 先触发执行
    try {
        task.get();
    } catch (...) {
    }
    
    auto result = task.try_get();
    TEST_EXPECT_TRUE(result == nullptr);
}

// 测试12: 不同类型的异常
TEST_CASE(custom_exception_type) {
    auto task = custom_exception_task();
    
    bool exception_caught = false;
    try {
        task.get();
    } catch (const std::logic_error& e) {
        exception_caught = true;
        TEST_EXPECT_TRUE(std::string(e.what()) == "Logic error exception");
    }
    
    TEST_EXPECT_TRUE(exception_caught);
}

// 主函数
int main() {
    TEST_SUITE("Exception Propagation Tests");
    
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
