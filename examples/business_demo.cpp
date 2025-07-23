// 业务场景示例：电商订单处理系统
#include "../include/flowcoro.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>

using namespace flowcoro;

// 模拟业务数据结构
struct User {
    int id;
    std::string name;
    std::string email;
};

struct Product {
    int id;
    std::string name;
    double price;
    int stock;
};

struct Order {
    int id;
    int user_id;
    std::vector<int> product_ids;
    double total_amount;
    std::string status;
};

// =====================================================
// 业务场景1：并发处理多个API调用
// =====================================================

// 模拟查询用户信息（比如从Redis缓存或数据库）
Task<User> fetch_user(int user_id) {
    std::cout << "  📋 开始查询用户 " << user_id << "..." << std::endl;
    
    // 模拟网络延迟
    co_await sleep_for(std::chrono::milliseconds(50));
    
    co_return User{user_id, "用户" + std::to_string(user_id), "user" + std::to_string(user_id) + "@example.com"};
}

// 模拟查询商品信息
Task<Product> fetch_product(int product_id) {
    std::cout << "  🛍️  开始查询商品 " << product_id << "..." << std::endl;
    
    // 模拟数据库查询延迟
    co_await sleep_for(std::chrono::milliseconds(30));
    
    co_return Product{product_id, "商品" + std::to_string(product_id), 99.9 + product_id, 100};
}

// 模拟检查库存
Task<bool> check_stock(int product_id, int quantity) {
    std::cout << "  📦 检查商品 " << product_id << " 库存..." << std::endl;
    
    co_await sleep_for(std::chrono::milliseconds(20));
    
    // 模拟库存检查逻辑
    co_return (product_id % 10) != 0; // 假设ID为10的倍数时缺货
}

// 业务核心：并发处理订单创建
Task<Order> create_order_async(int user_id, std::vector<int> product_ids) {
    std::cout << "🚀 开始异步创建订单..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 同时发起多个异步操作（这是协程的核心优势！）
    auto user_task = fetch_user(user_id);
    
    std::vector<Task<Product>> product_tasks;
    std::vector<Task<bool>> stock_tasks;
    
    for (int pid : product_ids) {
        product_tasks.push_back(fetch_product(pid));
        stock_tasks.push_back(check_stock(pid, 1));
    }
    
    // 等待所有异步操作完成
    User user = co_await user_task;
    std::cout << "✅ 用户信息获取完成: " << user.name << std::endl;
    
    std::vector<Product> products;
    double total = 0.0;
    
    for (size_t i = 0; i < product_tasks.size(); ++i) {
        Product product = co_await product_tasks[i];
        bool in_stock = co_await stock_tasks[i];
        
        if (!in_stock) {
            throw std::runtime_error("商品 " + product.name + " 库存不足");
        }
        
        products.push_back(product);
        total += product.price;
        std::cout << "✅ 商品检查完成: " << product.name << " - ¥" << product.price << std::endl;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    Order order{12345, user_id, product_ids, total, "已创建"};
    std::cout << "🎉 订单创建成功！用时: " << duration.count() << "ms" << std::endl;
    std::cout << "   订单金额: ¥" << total << std::endl;
    
    co_return order;
}

// =====================================================
// 业务场景2：批量处理多个订单
// =====================================================

Task<void> process_orders_batch(std::vector<int> user_ids) {
    std::cout << "\n🔄 开始批量处理订单..." << std::endl;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::vector<Task<Order>> order_tasks;
    
    // 并发创建多个订单（真实业务场景）
    for (int user_id : user_ids) {
        std::vector<int> products = {1, 2, 3}; // 模拟购物车
        order_tasks.push_back(create_order_async(user_id, products));
    }
    
    // 等待所有订单处理完成
    for (size_t i = 0; i < order_tasks.size(); ++i) {
        try {
            Order order = co_await order_tasks[i];
            std::cout << "✅ 用户 " << user_ids[i] << " 的订单处理完成" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "❌ 用户 " << user_ids[i] << " 的订单处理失败: " << e.what() << std::endl;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "🏁 批量处理完成，总用时: " << duration.count() << "ms" << std::endl;
}

// =====================================================
// 对比演示：同步vs异步的性能差异
// =====================================================

// 传统同步方式
Order create_order_sync(int user_id, std::vector<int> product_ids) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 串行执行，每个操作都要等待
    std::cout << "😴 同步方式：逐个等待每个操作..." << std::endl;
    
    // 查用户（等待50ms）
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    User user{user_id, "用户" + std::to_string(user_id), "sync@example.com"};
    
    double total = 0.0;
    for (int pid : product_ids) {
        // 查商品（等待30ms）
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        // 查库存（等待20ms）
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        total += 99.9 + pid;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    std::cout << "🐌 同步方式完成，用时: " << duration.count() << "ms" << std::endl;
    
    return Order{12346, user_id, product_ids, total, "已创建(同步)"};
}

int main() {
    std::cout << "=====================================\n";
    std::cout << "🏪 FlowCoro 电商业务场景演示\n";
    std::cout << "=====================================\n\n";
    
    try {
        // 场景1：单个订单异步处理
        std::cout << "📝 场景1：异步处理单个订单\n";
        std::cout << "-------------------------------------\n";
        auto async_task = create_order_async(1001, {1, 2, 3});
        auto async_order = async_task.get();
        
        std::cout << "\n📝 场景2：传统同步方式对比\n";
        std::cout << "-------------------------------------\n";
        auto sync_order = create_order_sync(1002, {1, 2, 3});
        
        std::cout << "\n📊 性能对比结果：\n";
        std::cout << "   异步协程：并发执行，大幅提升性能\n";
        std::cout << "   同步方式：串行等待，性能较差\n";
        
        // 场景3：批量订单处理
        std::cout << "\n📝 场景3：批量处理多个订单\n";
        std::cout << "-------------------------------------\n";
        auto batch_task = process_orders_batch({2001, 2002, 2003});
        batch_task.get();
        
        std::cout << "\n🎉 所有业务场景演示完成！\n";
        std::cout << "\n💡 关键优势：\n";
        std::cout << "   ✅ 并发处理多个IO操作\n";
        std::cout << "   ✅ 显著提升系统性能\n";
        std::cout << "   ✅ 代码简洁易懂\n";
        std::cout << "   ✅ 异常处理机制完善\n";
        
    } catch (const std::exception& e) {
        std::cerr << "❌ 业务处理出错: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
