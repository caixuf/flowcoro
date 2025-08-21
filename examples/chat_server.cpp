#include "flowcoro/net.h"
#include "flowcoro/core.h"
#include <iostream>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <cstring>

using namespace flowcoro;
using namespace flowcoro::net;

std::atomic<bool> server_running{true};
std::string server_name = "FlowCoro聊天室";
int server_port = 8080;

// 获取当前时间字符串
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// 打印带时间戳的日志
void log_info(const std::string& message) {
    std::cout << "[" << get_timestamp() << "] " << message << std::endl;
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        log_info("收到关闭信号，正在优雅停止服务器...");
        server_running.store(false);
    }
}

class ChatServer {
private:
    std::vector<std::shared_ptr<TcpConnection>> clients;
    std::unordered_map<std::shared_ptr<TcpConnection>, std::string> client_names;
    std::mutex clients_mutex;
    int next_client_id = 1;

public:
    void add_client(std::shared_ptr<TcpConnection> client) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        std::string client_name = "用户" + std::to_string(next_client_id++);
        clients.push_back(client);
        client_names[client] = client_name;
        
        log_info(client_name + " 加入聊天室 (在线人数: " + std::to_string(clients.size()) + ")");
        
        // 发送欢迎消息
        std::string welcome = "=== 欢迎来到 " + server_name + " ===\n";
        welcome += "你的昵称是: " + client_name + "\n";
        welcome += "当前在线人数: " + std::to_string(clients.size()) + "\n";
        welcome += "输入消息开始聊天，输入 /help 查看命令\n";
        
        try {
            client->write(welcome);
            client->flush();
        } catch (...) {}
        
        // 通知其他用户
        broadcast_system_message(client_name + " 加入了聊天室", client);
    }

    void remove_client(std::shared_ptr<TcpConnection> client) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = client_names.find(client);
        std::string client_name = (it != client_names.end()) ? it->second : "未知用户";
        
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
        if (it != client_names.end()) {
            client_names.erase(it);
        }
        
        log_info(client_name + " 离开聊天室 (在线人数: " + std::to_string(clients.size()) + ")");
        
        // 通知其他用户
        if (!clients.empty()) {
            broadcast_system_message(client_name + " 离开了聊天室", nullptr);
        }
    }

    Task<void> broadcast_system_message(const std::string& message, std::shared_ptr<TcpConnection> exclude) {
        std::string formatted = "[系统] " + message + "\n";
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& client : clients) {
            if (client != exclude && !client->is_closed()) {
                try {
                    co_await client->write(formatted);
                    co_await client->flush();
                } catch (...) {}
            }
        }
    }

    Task<void> broadcast_user_message(const std::string& message, std::shared_ptr<TcpConnection> sender) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = client_names.find(sender);
        std::string sender_name = (it != client_names.end()) ? it->second : "未知用户";
        
        std::string formatted = "[" + get_timestamp() + "] " + sender_name + ": " + message;
        if (formatted.back() != '\n') formatted += "\n";
        
        for (auto& client : clients) {
            if (client != sender && !client->is_closed()) {
                try {
                    co_await client->write(formatted);
                    co_await client->flush();
                } catch (...) {}
            }
        }
    }

    Task<void> handle_command(const std::string& command, std::shared_ptr<TcpConnection> client) {
        std::string response;
        
        if (command == "/help") {
            response = "=== 聊天室命令 ===\n";
            response += "/help - 显示此帮助\n";
            response += "/list - 显示在线用户列表\n";
            response += "/time - 显示服务器时间\n";
            response += "/stats - 显示服务器统计\n";
            response += "/quit - 退出聊天室\n";
        } else if (command == "/list") {
            std::lock_guard<std::mutex> lock(clients_mutex);
            response = "=== 在线用户 (" + std::to_string(clients.size()) + "人) ===\n";
            for (const auto& pair : client_names) {
                if (!pair.first->is_closed()) {
                    response += "- " + pair.second + "\n";
                }
            }
        } else if (command == "/time") {
            response = "服务器时间: " + get_timestamp() + "\n";
        } else if (command == "/stats") {
            std::lock_guard<std::mutex> lock(clients_mutex);
            response = "=== 服务器统计 ===\n";
            response += "服务器名称: " + server_name + "\n";
            response += "监听端口: " + std::to_string(server_port) + "\n";
            response += "在线用户: " + std::to_string(clients.size()) + "人\n";
            response += "服务器时间: " + get_timestamp() + "\n";
        } else if (command == "/quit") {
            response = "再见! 感谢使用 " + server_name + "\n";
            try {
                co_await client->write(response);
                co_await client->flush();
            } catch (...) {}
            client->close();
            co_return;
        } else {
            response = "未知命令: " + command + "，输入 /help 查看可用命令\n";
        }
        
        try {
            co_await client->write(response);
            co_await client->flush();
        } catch (...) {}
    }

    Task<void> handle_client(std::unique_ptr<Socket> sock) {
        auto connection = std::make_shared<TcpConnection>(std::move(sock));
        add_client(connection);
        
        try {
            while (!connection->is_closed()) {
                std::string line = co_await connection->read_line();
                if (line.empty()) {
                    break;
                }
                
                // 移除换行符
                if (!line.empty() && line.back() == '\n') {
                    line.pop_back();
                }
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                
                // 跳过空消息
                if (line.empty()) {
                    continue;
                }
                
                log_info("收到消息: " + line);
                
                // 处理命令
                if (line[0] == '/') {
                    co_await handle_command(line, connection);
                } else {
                    // 广播用户消息
                    co_await broadcast_user_message(line, connection);
                }
            }
        } catch (const std::exception& e) {
            log_info("客户端处理错误: " + std::string(e.what()));
        }
        
        remove_client(connection);
        connection->close();
    }
};

// 显示使用帮助
void show_usage(const char* program_name) {
    std::cout << "FlowCoro 聊天服务器 v1.0\n\n";
    std::cout << "用法: " << program_name << " [选项]\n\n";
    std::cout << "选项:\n";
    std::cout << "  -p, --port <端口号>    指定服务器端口 (默认: 8080)\n";
    std::cout << "  -n, --name <名称>      指定聊天室名称 (默认: FlowCoro聊天室)\n";
    std::cout << "  -h, --help             显示此帮助信息\n";
    std::cout << "\n示例:\n";
    std::cout << "  " << program_name << " -p 9999 -n \"我的聊天室\"\n";
    std::cout << "  " << program_name << " --port 8080\n\n";
}

int main(int argc, char* argv[]) {
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            show_usage(argv[0]);
            return 0;
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 < argc) {
                try {
                    server_port = std::stoi(argv[++i]);
                    if (server_port < 1024 || server_port > 65535) {
                        std::cerr << "错误: 端口号必须在 1024-65535 范围内\n";
                        return 1;
                    }
                } catch (...) {
                    std::cerr << "错误: 无效的端口号 '" << argv[i] << "'\n";
                    return 1;
                }
            } else {
                std::cerr << "错误: -p/--port 需要指定端口号\n";
                return 1;
            }
        } else if (arg == "-n" || arg == "--name") {
            if (i + 1 < argc) {
                server_name = argv[++i];
            } else {
                std::cerr << "错误: -n/--name 需要指定聊天室名称\n";
                return 1;
            }
        } else {
            std::cerr << "错误: 未知选项 '" << arg << "'\n";
            std::cerr << "使用 -h 或 --help 查看帮助\n";
            return 1;
        }
    }
    
    try {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        log_info("启动 FlowCoro 聊天服务器");
        log_info("聊天室名称: " + server_name);
        log_info("监听端口: " + std::to_string(server_port));

        ChatServer chat_server;
        auto& loop = GlobalEventLoop::get();
        TcpServer server(&loop);
        
        server.set_connection_handler([&chat_server](std::unique_ptr<Socket> sock) -> Task<void> {
            co_await chat_server.handle_client(std::move(sock));
        });

        log_info("正在启动网络服务...");
        sync_wait(server.listen("0.0.0.0", server_port));
        
        log_info("服务器启动成功! 等待客户端连接...");
        std::cout << "\n=== 连接方式 ===\n";
        std::cout << "telnet localhost " << server_port << "\n";
        std::cout << "nc localhost " << server_port << "\n";
        std::cout << "\n按 Ctrl+C 停止服务器\n" << std::endl;

        while (server_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        log_info("正在停止服务器...");
        server.stop();
        GlobalEventLoop::shutdown();
        log_info("服务器已停止");
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "服务器错误: " << e.what() << std::endl;
        return 1;
    }
}
