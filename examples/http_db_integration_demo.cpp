#include <flowcoro.hpp>
#include <flowcoro/http_client.h>
#include <flowcoro/simple_db.h>
#include <iostream>
#include <sstream>
#include <ctime>

using namespace flowcoro;
using namespace flowcoro::net;
using namespace flowcoro::db;

// HTTP API + 文件数据库集成演示
class HttpApiDemo {
private:
    HttpClient client_;
    SimpleFileDB db_;

public:
    HttpApiDemo() : client_(), db_("./api_demo_db") {}

    // 从HTTP API获取用户信息并存储到数据库
    Task<void> fetchAndStoreUser(const std::string& user_id) {
        std::cout << "🌐 Fetching user " << user_id << " from API..." << std::endl;
        
        // 调用REST API获取用户信息
        std::string url = "https://jsonplaceholder.typicode.com/users/" + user_id;
        auto response = co_await client_.get(url);
        
        if (!response.success) {
            std::cout << "❌ Failed to fetch user: " << response.error_message << std::endl;
            co_return;
        }

        std::cout << "✅ API Response: " << response.status_code << std::endl;
        
        // 解析JSON响应（简化版本）
        std::string json_data = response.body;
        
        // 创建文档存储到数据库
        auto users_collection = db_.collection("users");
        SimpleDocument user_doc("user_" + user_id);
        
        // 从JSON中提取字段（简化处理）
        size_t name_start = json_data.find("\"name\":\"") + 8;
        size_t name_end = json_data.find("\"", name_start);
        std::string name = json_data.substr(name_start, name_end - name_start);
        
        size_t email_start = json_data.find("\"email\":\"") + 9;
        size_t email_end = json_data.find("\"", email_start);
        std::string email = json_data.substr(email_start, email_end - email_start);
        
        user_doc.set("name", name);
        user_doc.set("email", email);
        user_doc.set("fetched_at", std::to_string(std::time(nullptr)));
        user_doc.set("source", "jsonplaceholder.typicode.com");
        
        bool stored = co_await users_collection->insert(user_doc);
        if (stored) {
            std::cout << "💾 User stored in database: " << name << " (" << email << ")" << std::endl;
        } else {
            std::cout << "❌ Failed to store user in database" << std::endl;
        }
    }

    // 从数据库查询用户并调用API更新
    Task<void> syncUserWithApi(const std::string& user_id) {
        std::cout << "\n🔄 Syncing user " << user_id << " with API..." << std::endl;
        
        auto users_collection = db_.collection("users");
        auto user_doc = co_await users_collection->find_by_id("user_" + user_id);
        
        if (user_doc.id.empty()) {
            std::cout << "❌ User not found in database" << std::endl;
            co_return;
        }
        
        std::cout << "📂 Found user in database: " << user_doc.get("name") << std::endl;
        
        // 调用API更新用户信息
        std::string url = "https://jsonplaceholder.typicode.com/users/" + user_id;
        std::string update_data = R"({
            "name": ")" + user_doc.get("name") + R"(",
            "email": ")" + user_doc.get("email") + R"(updated@flowcoro.com",
            "updated_by": "FlowCoro"
        })";
        
        auto response = co_await client_.post(url, update_data, {{"Content-Type", "application/json"}});
        
        if (response.success) {
            std::cout << "✅ API update successful: " << response.status_code << std::endl;
            
            // 更新本地数据库记录（重新插入）
            user_doc.set("last_synced", std::to_string(std::time(nullptr)));
            user_doc.set("sync_status", "success");
            co_await users_collection->insert(user_doc);
            
            std::cout << "💾 Local database updated" << std::endl;
        } else {
            std::cout << "❌ API update failed: " << response.error_message << std::endl;
        }
    }

    // 批量处理：获取多个用户并存储
    Task<void> batchFetchUsers(const std::vector<std::string>& user_ids) {
        std::cout << "\n📋 Batch fetching " << user_ids.size() << " users..." << std::endl;
        
        std::vector<Task<void>> tasks;
        for (const auto& user_id : user_ids) {
            tasks.push_back(fetchAndStoreUser(user_id));
        }
        
        // 并发执行所有请求
        for (auto& task : tasks) {
            co_await task;
        }
        
        std::cout << "✅ Batch operation completed" << std::endl;
    }

    // 数据库统计报告
    Task<void> generateReport() {
        std::cout << "\n📊 Generating database report..." << std::endl;
        
        auto users_collection = db_.collection("users");
        auto all_users = co_await users_collection->find_all();
        
        std::cout << "📈 Total users in database: " << all_users.size() << std::endl;
        
        for (const auto& user : all_users) {
            std::cout << "👤 " << user.get("name") << " (" << user.get("email") << ")" 
                      << " - Source: " << user.get("source") << std::endl;
        }
        
        auto db_info = co_await db_.get_info();
        std::cout << "\n🗂️  Database Info:" << std::endl;
        for (const auto& [key, value] : db_info) {
            std::cout << "  " << key << ": " << value << std::endl;
        }
    }

    // 清理数据库
    Task<void> cleanup() {
        std::cout << "\n🧹 Cleaning up database..." << std::endl;
        
        auto collections = db_.list_collections();
        for (const auto& collection_name : collections) {
            bool dropped = db_.drop_collection(collection_name);
            if (dropped) {
                std::cout << "🗑️  Dropped collection: " << collection_name << std::endl;
            }
        }
    }
};

Task<void> runIntegrationDemo() {
    std::cout << "🚀 FlowCoro HTTP + Database Integration Demo" << std::endl;
    std::cout << "================================================" << std::endl;
    
    HttpApiDemo demo;
    
    try {
        // 1. 获取并存储用户数据
        co_await demo.batchFetchUsers({"1", "2", "3"});
        
        // 2. 生成报告
        co_await demo.generateReport();
        
        // 3. 同步用户数据
        co_await demo.syncUserWithApi("1");
        
        // 4. 最终报告
        co_await demo.generateReport();
        
        // 5. 清理（可选）
        std::cout << "\n❓ Clean up database? (y/n): ";
        std::string choice;
        std::getline(std::cin, choice);
        if (choice == "y" || choice == "Y") {
            co_await demo.cleanup();
            std::cout << "✅ Database cleaned up" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "💥 Demo error: " << e.what() << std::endl;
    }
    
    std::cout << "\n🎯 Integration demo completed!" << std::endl;
}

int main() {
    try {
        sync_wait(runIntegrationDemo());
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
