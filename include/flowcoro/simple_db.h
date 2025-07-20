#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <filesystem>

#include "core.h"

namespace flowcoro::db {

// 简单的JSON-like数据格式
struct SimpleDocument {
    std::unordered_map<std::string, std::string> fields;
    std::string id;
    
    SimpleDocument() = default;
    SimpleDocument(const std::string& doc_id) : id(doc_id) {}
    
    void set(const std::string& key, const std::string& value) {
        fields[key] = value;
    }
    
    std::string get(const std::string& key, const std::string& default_value = "") const {
        auto it = fields.find(key);
        return (it != fields.end()) ? it->second : default_value;
    }
    
    bool has(const std::string& key) const {
        return fields.find(key) != fields.end();
    }
    
    // 序列化为简单格式
    std::string serialize() const {
        std::ostringstream ss;
        ss << "{\"id\":\"" << id << "\"";
        for (const auto& [key, value] : fields) {
            ss << ",\"" << key << "\":\"" << value << "\"";
        }
        ss << "}";
        return ss.str();
    }
    
    // 从简单格式反序列化
    static SimpleDocument deserialize(const std::string& data) {
        SimpleDocument doc;
        
        // 简单的JSON解析（仅支持字符串字段）
        size_t start = data.find("{");
        size_t end = data.find("}", start);
        if (start == std::string::npos || end == std::string::npos) {
            return doc;
        }
        
        std::string content = data.substr(start + 1, end - start - 1);
        std::istringstream ss(content);
        std::string item;
        
        while (std::getline(ss, item, ',')) {
            size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            
            std::string key = item.substr(0, colon);
            std::string value = item.substr(colon + 1);
            
            // 去除引号
            auto remove_quotes = [](std::string& s) {
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
                    s = s.substr(1, s.size() - 2);
                }
            };
            
            remove_quotes(key);
            remove_quotes(value);
            
            if (key == "id") {
                doc.id = value;
            } else {
                doc.fields[key] = value;
            }
        }
        
        return doc;
    }
};

// 文件数据库集合
class FileCollection {
private:
    std::string collection_name_;
    std::string file_path_;
    mutable std::mutex mutex_;
    
public:
    FileCollection(const std::string& db_path, const std::string& collection_name) 
        : collection_name_(collection_name) {
        
        // 确保数据库目录存在
        std::filesystem::create_directories(db_path);
        file_path_ = db_path + "/" + collection_name + ".db";
    }
    
    // 插入文档
    Task<bool> insert(const SimpleDocument& doc) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ofstream file(file_path_, std::ios::app);
        if (!file.is_open()) {
            co_return false;
        }
        
        file << doc.serialize() << std::endl;
        file.close();
        co_return true;
    }
    
    // 查找文档
    Task<SimpleDocument> find_by_id(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::ifstream file(file_path_);
        if (!file.is_open()) {
            co_return SimpleDocument();
        }
        
        std::string line;
        while (std::getline(file, line)) {
            auto doc = SimpleDocument::deserialize(line);
            if (doc.id == id) {
                co_return doc;
            }
        }
        
        co_return SimpleDocument();
    }
    
    // 查找所有文档
    Task<std::vector<SimpleDocument>> find_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<SimpleDocument> results;
        std::ifstream file(file_path_);
        if (!file.is_open()) {
            co_return results;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                results.push_back(SimpleDocument::deserialize(line));
            }
        }
        
        co_return results;
    }
    
    // 按字段查找
    Task<std::vector<SimpleDocument>> find_by_field(const std::string& field, const std::string& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<SimpleDocument> results;
        std::ifstream file(file_path_);
        if (!file.is_open()) {
            co_return results;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                auto doc = SimpleDocument::deserialize(line);
                if (doc.get(field) == value) {
                    results.push_back(doc);
                }
            }
        }
        
        co_return results;
    }
    
    // 更新文档（简单实现：重写整个文件）
    Task<bool> update_by_id(const std::string& id, const SimpleDocument& new_doc) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<SimpleDocument> all_docs;
        
        // 读取所有文档
        std::ifstream read_file(file_path_);
        if (read_file.is_open()) {
            std::string line;
            while (std::getline(read_file, line)) {
                if (!line.empty()) {
                    auto doc = SimpleDocument::deserialize(line);
                    if (doc.id == id) {
                        all_docs.push_back(new_doc);
                    } else {
                        all_docs.push_back(doc);
                    }
                }
            }
            read_file.close();
        }
        
        // 重写文件
        std::ofstream write_file(file_path_);
        if (!write_file.is_open()) {
            co_return false;
        }
        
        for (const auto& doc : all_docs) {
            write_file << doc.serialize() << std::endl;
        }
        
        write_file.close();
        co_return true;
    }
    
    // 删除文档
    Task<bool> delete_by_id(const std::string& id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<SimpleDocument> remaining_docs;
        
        // 读取除了要删除的文档之外的所有文档
        std::ifstream read_file(file_path_);
        if (read_file.is_open()) {
            std::string line;
            while (std::getline(read_file, line)) {
                if (!line.empty()) {
                    auto doc = SimpleDocument::deserialize(line);
                    if (doc.id != id) {
                        remaining_docs.push_back(doc);
                    }
                }
            }
            read_file.close();
        }
        
        // 重写文件
        std::ofstream write_file(file_path_);
        if (!write_file.is_open()) {
            co_return false;
        }
        
        for (const auto& doc : remaining_docs) {
            write_file << doc.serialize() << std::endl;
        }
        
        write_file.close();
        co_return true;
    }
    
    // 统计文档数量
    Task<size_t> count() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        size_t count = 0;
        std::ifstream file(file_path_);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    count++;
                }
            }
        }
        
        co_return count;
    }
};

// 简单文件数据库
class SimpleFileDB {
private:
    std::string db_path_;
    std::unordered_map<std::string, std::shared_ptr<FileCollection>> collections_;
    mutable std::mutex mutex_;
    
public:
    SimpleFileDB(const std::string& db_path) : db_path_(db_path) {
        std::filesystem::create_directories(db_path_);
    }
    
    // 获取集合
    std::shared_ptr<FileCollection> collection(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto it = collections_.find(name);
        if (it != collections_.end()) {
            return it->second;
        }
        
        auto new_collection = std::make_shared<FileCollection>(db_path_, name);
        collections_[name] = new_collection;
        return new_collection;
    }
    
    // 列出所有集合
    std::vector<std::string> list_collections() {
        std::vector<std::string> names;
        
        try {
            for (const auto& entry : std::filesystem::directory_iterator(db_path_)) {
                if (entry.is_regular_file() && entry.path().extension() == ".db") {
                    std::string name = entry.path().stem().string();
                    names.push_back(name);
                }
            }
        } catch (const std::filesystem::filesystem_error&) {
            // 忽略文件系统错误
        }
        
        return names;
    }
    
    // 删除集合
    bool drop_collection(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string file_path = db_path_ + "/" + name + ".db";
        
        try {
            bool removed = std::filesystem::remove(file_path);
            if (removed) {
                collections_.erase(name);
            }
            return removed;
        } catch (const std::filesystem::filesystem_error&) {
            return false;
        }
    }
    
    // 获取数据库信息
    Task<std::unordered_map<std::string, std::string>> get_info() {
        std::unordered_map<std::string, std::string> info;
        
        info["database_path"] = db_path_;
        info["type"] = "SimpleFileDB";
        info["version"] = "1.0.0";
        
        auto collection_names = list_collections();
        info["collections_count"] = std::to_string(collection_names.size());
        
        std::string collections_list;
        for (size_t i = 0; i < collection_names.size(); ++i) {
            if (i > 0) collections_list += ", ";
            collections_list += collection_names[i];
        }
        info["collections"] = collections_list;
        
        co_return info;
    }
};

} // namespace flowcoro::db
