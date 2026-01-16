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
std::string server_name = "FlowCoro";
int server_port = 8080;

// 
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

// 
void log_info(const std::string& message) {
    std::cout << "[" << get_timestamp() << "] " << message << std::endl;
}

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        log_info("...");
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
        std::string client_name = "" + std::to_string(next_client_id++);
        clients.push_back(client);
        client_names[client] = client_name;
        
        log_info(client_name + "  (: " + std::to_string(clients.size()) + ")");
        
        // 
        std::string welcome = "===  " + server_name + " ===\n";
        welcome += ": " + client_name + "\n";
        welcome += ": " + std::to_string(clients.size()) + "\n";
        welcome += " /help \n";
        
        try {
            client->write(welcome);
            client->flush();
        } catch (...) {}
        
        // 
        broadcast_system_message(client_name + " ", client);
    }

    void remove_client(std::shared_ptr<TcpConnection> client) {
        std::lock_guard<std::mutex> lock(clients_mutex);
        auto it = client_names.find(client);
        std::string client_name = (it != client_names.end()) ? it->second : "";
        
        clients.erase(std::remove(clients.begin(), clients.end(), client), clients.end());
        if (it != client_names.end()) {
            client_names.erase(it);
        }
        
        log_info(client_name + "  (: " + std::to_string(clients.size()) + ")");
        
        // 
        if (!clients.empty()) {
            broadcast_system_message(client_name + " ", nullptr);
        }
    }

    Task<void> broadcast_system_message(const std::string& message, std::shared_ptr<TcpConnection> exclude) {
        std::string formatted = "[] " + message + "\n";
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
        std::string sender_name = (it != client_names.end()) ? it->second : "";
        
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
            response = "===  ===\n";
            response += "/help - \n";
            response += "/list - \n";
            response += "/time - \n";
            response += "/stats - \n";
            response += "/quit - \n";
        } else if (command == "/list") {
            std::lock_guard<std::mutex> lock(clients_mutex);
            response = "===  (" + std::to_string(clients.size()) + ") ===\n";
            for (const auto& pair : client_names) {
                if (!pair.first->is_closed()) {
                    response += "- " + pair.second + "\n";
                }
            }
        } else if (command == "/time") {
            response = ": " + get_timestamp() + "\n";
        } else if (command == "/stats") {
            std::lock_guard<std::mutex> lock(clients_mutex);
            response = "===  ===\n";
            response += ": " + server_name + "\n";
            response += ": " + std::to_string(server_port) + "\n";
            response += ": " + std::to_string(clients.size()) + "\n";
            response += ": " + get_timestamp() + "\n";
        } else if (command == "/quit") {
            response = "!  " + server_name + "\n";
            try {
                co_await client->write(response);
                co_await client->flush();
            } catch (...) {}
            client->close();
            co_return;
        } else {
            response = ": " + command + " /help \n";
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
                
                // 
                if (!line.empty() && line.back() == '\n') {
                    line.pop_back();
                }
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                
                // 
                if (line.empty()) {
                    continue;
                }
                
                log_info(": " + line);
                
                // 
                if (line[0] == '/') {
                    co_await handle_command(line, connection);
                } else {
                    // 
                    co_await broadcast_user_message(line, connection);
                }
            }
        } catch (const std::exception& e) {
            log_info(": " + std::string(e.what()));
        }
        
        remove_client(connection);
        connection->close();
    }
};

// 
void show_usage(const char* program_name) {
    std::cout << "FlowCoro  v1.0\n\n";
    std::cout << ": " << program_name << " []\n\n";
    std::cout << ":\n";
    std::cout << "  -p, --port <>     (: 8080)\n";
    std::cout << "  -n, --name <>       (: FlowCoro)\n";
    std::cout << "  -h, --help             \n";
    std::cout << "\n:\n";
    std::cout << "  " << program_name << " -p 9999 -n \"\"\n";
    std::cout << "  " << program_name << " --port 8080\n\n";
}

int main(int argc, char* argv[]) {
    // 
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
                        std::cerr << ":  1024-65535 \n";
                        return 1;
                    }
                } catch (...) {
                    std::cerr << ":  '" << argv[i] << "'\n";
                    return 1;
                }
            } else {
                std::cerr << ": -p/--port \n";
                return 1;
            }
        } else if (arg == "-n" || arg == "--name") {
            if (i + 1 < argc) {
                server_name = argv[++i];
            } else {
                std::cerr << ": -n/--name \n";
                return 1;
            }
        } else {
            std::cerr << ":  '" << arg << "'\n";
            std::cerr << " -h  --help \n";
            return 1;
        }
    }
    
    try {
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        log_info(" FlowCoro ");
        log_info(": " + server_name);
        log_info(": " + std::to_string(server_port));

        ChatServer chat_server;
        auto& loop = GlobalEventLoop::get();
        TcpServer server(&loop);
        
        server.set_connection_handler([&chat_server](std::unique_ptr<Socket> sock) -> Task<void> {
            co_await chat_server.handle_client(std::move(sock));
        });

        log_info("...");
        sync_wait(server.listen("0.0.0.0", server_port));
        
        log_info("! ...");
        std::cout << "\n===  ===\n";
        std::cout << "telnet localhost " << server_port << "\n";
        std::cout << "nc localhost " << server_port << "\n";
        std::cout << "\n Ctrl+C \n" << std::endl;

        while (server_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        log_info("...");
        server.stop();
        GlobalEventLoop::shutdown();
        log_info("");
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << ": " << e.what() << std::endl;
        return 1;
    }
}
