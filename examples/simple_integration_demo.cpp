#include <flowcoro.hpp>
#include <flowcoro/http_client.h>
#include <flowcoro/simple_db.h>
#include <iostream>

using namespace flowcoro;
using namespace flowcoro::net;
using namespace flowcoro::db;

Task<void> simpleHttpAndDbDemo() {
    std::cout << "🚀 FlowCoro HTTP + Database Simple Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 1. 测试HTTP客户端
    std::cout << "\n🌐 Testing HTTP Client..." << std::endl;
    HttpClient client;
    auto response = co_await client.get("http://httpbin.org/json");
    
    if (response.success) {
        std::cout << "✅ HTTP GET successful: " << response.status_code << std::endl;
        std::cout << "📄 Response body length: " << response.body.size() << " bytes" << std::endl;
    } else {
        std::cout << "❌ HTTP GET failed: " << response.error_message << std::endl;
    }
    
    // 2. 测试文件数据库
    std::cout << "\n🗄️  Testing File Database..." << std::endl;
    SimpleFileDB db("./demo_db");
    
    auto users_collection = db.collection("demo_users");
    
    // 插入数据
    SimpleDocument user1("demo_user_1");
    user1.set("name", "Alice Johnson");
    user1.set("email", "alice@demo.com");
    user1.set("role", "developer");
    
    bool inserted = co_await users_collection->insert(user1);
    if (inserted) {
        std::cout << "✅ User inserted successfully" << std::endl;
    }
    
    // 查询数据
    auto found_user = co_await users_collection->find_by_id("demo_user_1");
    if (!found_user.id.empty()) {
        std::cout << "✅ User found: " << found_user.get("name") 
                  << " (" << found_user.get("email") << ")" << std::endl;
    }
    
    // 统计数据
    auto count = co_await users_collection->count();
    std::cout << "📊 Total users in collection: " << count << std::endl;
    
    // 3. 综合演示：从HTTP获取数据并存储
    std::cout << "\n🔄 Integration Demo: HTTP -> Database..." << std::endl;
    
    auto http_response = co_await client.get("http://httpbin.org/uuid");
    if (http_response.success) {
        std::cout << "✅ Got UUID from HTTP API" << std::endl;
        
        // 将HTTP响应存储到数据库
        SimpleDocument api_data("api_response_1");
        api_data.set("source", "httpbin.org/uuid");
        api_data.set("response_code", std::to_string(http_response.status_code));
        api_data.set("data", http_response.body.substr(0, 100)); // 限制长度
        api_data.set("timestamp", std::to_string(std::time(nullptr)));
        
        auto api_collection = db.collection("api_responses");
        bool stored = co_await api_collection->insert(api_data);
        
        if (stored) {
            std::cout << "✅ HTTP response stored in database" << std::endl;
        }
    }
    
    // 4. 生成最终报告
    std::cout << "\n📋 Final Report:" << std::endl;
    auto collections = db.list_collections();
    std::cout << "📂 Database collections: " << collections.size() << std::endl;
    
    for (const auto& collection_name : collections) {
        auto collection = db.collection(collection_name);
        auto doc_count = co_await collection->count();
        std::cout << "  📁 " << collection_name << ": " << doc_count << " documents" << std::endl;
    }
    
    std::cout << "\n🎯 Demo completed successfully!" << std::endl;
}

int main() {
    try {
        sync_wait(simpleHttpAndDbDemo());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Demo error: " << e.what() << std::endl;
        return 1;
    }
}
