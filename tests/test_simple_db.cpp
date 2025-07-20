#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

#include "../include/flowcoro.hpp"
#include "../include/flowcoro/simple_db.h"
#include "test_framework.h"

using namespace flowcoro;
using namespace flowcoro::test;
using namespace flowcoro::db;

// 测试简单文档操作
TEST_CASE(simple_document_operations) {
    SimpleDocument doc("test_id_123");
    doc.set("name", "Test User");
    doc.set("email", "test@example.com");
    doc.set("age", "25");
    
    TEST_EXPECT_EQ(doc.id, "test_id_123");
    TEST_EXPECT_EQ(doc.get("name"), "Test User");
    TEST_EXPECT_EQ(doc.get("email"), "test@example.com");
    TEST_EXPECT_EQ(doc.get("age"), "25");
    TEST_EXPECT_TRUE(doc.has("name"));
    TEST_EXPECT_FALSE(doc.has("nonexistent"));
    
    // 测试序列化和反序列化
    std::string serialized = doc.serialize();
    TEST_EXPECT_TRUE(serialized.find("Test User") != std::string::npos);
    
    SimpleDocument deserialized = SimpleDocument::deserialize(serialized);
    TEST_EXPECT_EQ(deserialized.id, "test_id_123");
    TEST_EXPECT_EQ(deserialized.get("name"), "Test User");
    TEST_EXPECT_EQ(deserialized.get("email"), "test@example.com");
}

// 测试文件集合基本操作
TEST_CASE(file_collection_basic_ops) {
    std::string test_db_path = "./test_db";
    
    // 清理测试数据库
    std::filesystem::remove_all(test_db_path);
    
    FileCollection collection(test_db_path, "users");
    
    // 插入文档
    SimpleDocument user1("user1");
    user1.set("name", "Alice");
    user1.set("email", "alice@example.com");
    user1.set("department", "Engineering");
    
    auto insert_result = sync_wait(collection.insert(user1));
    TEST_EXPECT_TRUE(insert_result);
    
    // 查找文档
    auto found_user = sync_wait(collection.find_by_id("user1"));
    TEST_EXPECT_EQ(found_user.id, "user1");
    TEST_EXPECT_EQ(found_user.get("name"), "Alice");
    TEST_EXPECT_EQ(found_user.get("email"), "alice@example.com");
    
    // 插入更多文档
    SimpleDocument user2("user2");
    user2.set("name", "Bob");
    user2.set("email", "bob@example.com");
    user2.set("department", "Marketing");
    
    SimpleDocument user3("user3");
    user3.set("name", "Charlie");
    user3.set("email", "charlie@example.com");
    user3.set("department", "Engineering");
    
    sync_wait(collection.insert(user2));
    sync_wait(collection.insert(user3));
    
    // 按字段查找
    auto engineers = sync_wait(collection.find_by_field("department", "Engineering"));
    TEST_EXPECT_EQ(engineers.size(), 2);
    
    // 统计文档数量
    auto count = sync_wait(collection.count());
    TEST_EXPECT_EQ(count, 3);
    
    // 查找所有文档
    auto all_users = sync_wait(collection.find_all());
    TEST_EXPECT_EQ(all_users.size(), 3);
    
    // 清理测试数据
    std::filesystem::remove_all(test_db_path);
}

// 测试文档更新和删除
TEST_CASE(file_collection_update_delete) {
    std::string test_db_path = "./test_db_update";
    
    // 清理测试数据库
    std::filesystem::remove_all(test_db_path);
    
    FileCollection collection(test_db_path, "products");
    
    // 插入初始文档
    SimpleDocument product("prod1");
    product.set("name", "Widget");
    product.set("price", "10.99");
    product.set("category", "Tools");
    
    sync_wait(collection.insert(product));
    
    // 更新文档
    SimpleDocument updated_product("prod1");
    updated_product.set("name", "Super Widget");
    updated_product.set("price", "12.99");
    updated_product.set("category", "Tools");
    updated_product.set("description", "An amazing widget");
    
    auto update_result = sync_wait(collection.update_by_id("prod1", updated_product));
    TEST_EXPECT_TRUE(update_result);
    
    // 验证更新
    auto found_product = sync_wait(collection.find_by_id("prod1"));
    TEST_EXPECT_EQ(found_product.get("name"), "Super Widget");
    TEST_EXPECT_EQ(found_product.get("price"), "12.99");
    TEST_EXPECT_EQ(found_product.get("description"), "An amazing widget");
    
    // 删除文档
    auto delete_result = sync_wait(collection.delete_by_id("prod1"));
    TEST_EXPECT_TRUE(delete_result);
    
    // 验证删除
    auto deleted_product = sync_wait(collection.find_by_id("prod1"));
    TEST_EXPECT_TRUE(deleted_product.id.empty());
    
    auto count = sync_wait(collection.count());
    TEST_EXPECT_EQ(count, 0);
    
    // 清理测试数据
    std::filesystem::remove_all(test_db_path);
}

// 测试简单文件数据库
TEST_CASE(simple_file_db_operations) {
    std::string test_db_path = "./test_main_db";
    
    // 清理测试数据库
    std::filesystem::remove_all(test_db_path);
    
    SimpleFileDB db(test_db_path);
    
    // 获取集合
    auto users_collection = db.collection("users");
    auto orders_collection = db.collection("orders");
    
    // 在用户集合中插入数据
    SimpleDocument user("u001");
    user.set("username", "john_doe");
    user.set("email", "john@example.com");
    
    sync_wait(users_collection->insert(user));
    
    // 在订单集合中插入数据
    SimpleDocument order("o001");
    order.set("user_id", "u001");
    order.set("product", "Laptop");
    order.set("amount", "1299.99");
    
    sync_wait(orders_collection->insert(order));
    
    // 验证数据
    auto found_user = sync_wait(users_collection->find_by_id("u001"));
    TEST_EXPECT_EQ(found_user.get("username"), "john_doe");
    
    auto found_order = sync_wait(orders_collection->find_by_id("o001"));
    TEST_EXPECT_EQ(found_order.get("product"), "Laptop");
    
    // 列出集合
    auto collections = db.list_collections();
    TEST_EXPECT_EQ(collections.size(), 2);
    TEST_EXPECT_TRUE(std::find(collections.begin(), collections.end(), "users") != collections.end());
    TEST_EXPECT_TRUE(std::find(collections.begin(), collections.end(), "orders") != collections.end());
    
    // 获取数据库信息
    auto db_info = sync_wait(db.get_info());
    TEST_EXPECT_EQ(db_info["type"], "SimpleFileDB");
    TEST_EXPECT_EQ(db_info["collections_count"], "2");
    
    std::cout << "Database info:" << std::endl;
    for (const auto& [key, value] : db_info) {
        std::cout << "  " << key << ": " << value << std::endl;
    }
    
    // 删除集合
    bool dropped = db.drop_collection("orders");
    TEST_EXPECT_TRUE(dropped);
    
    auto collections_after_drop = db.list_collections();
    TEST_EXPECT_EQ(collections_after_drop.size(), 1);
    
    // 清理测试数据
    std::filesystem::remove_all(test_db_path);
}

// 测试并发操作
TEST_CASE(concurrent_database_operations) {
    std::string test_db_path = "./test_concurrent_db";
    
    // 清理测试数据库
    std::filesystem::remove_all(test_db_path);
    
    SimpleFileDB db(test_db_path);
    auto collection = db.collection("concurrent_test");
    
    // 并发插入文档
    std::vector<Task<bool>> insert_tasks;
    for (int i = 0; i < 5; ++i) {
        SimpleDocument doc("doc" + std::to_string(i));
        doc.set("value", "data" + std::to_string(i));
        doc.set("timestamp", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count()));
        
        insert_tasks.push_back(collection->insert(doc));
    }
    
    // 等待所有插入完成
    int successful_inserts = 0;
    for (auto& task : insert_tasks) {
        if (sync_wait(std::move(task))) {
            successful_inserts++;
        }
    }
    
    TEST_EXPECT_EQ(successful_inserts, 5);
    
    // 验证所有文档都被插入
    auto count = sync_wait(collection->count());
    TEST_EXPECT_EQ(count, 5);
    
    auto all_docs = sync_wait(collection->find_all());
    TEST_EXPECT_EQ(all_docs.size(), 5);
    
    // 清理测试数据
    std::filesystem::remove_all(test_db_path);
}

int main() {
    TEST_SUITE("FlowCoro Simple File Database Tests");
    
    std::cout << "测试简单文件数据库实现" << std::endl;
    std::cout << "支持文档存储、查询、更新和删除操作" << std::endl;
    std::cout << std::string(50, '-') << std::endl;
    
    TestRunner::print_summary();
    
    return TestRunner::all_passed() ? 0 : 1;
}
