#include "flowcoro/net.h" 
#include "flowcoro/core.h"
#include <iostream>
#include <csignal>
#include <atomic>

using namespace flowcoro;
using namespace flowcoro::net;

std::atomic<bool> server_running{true};

// 
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived shutdown signal, stopping server..." << std::endl;
        server_running.store(false);
    }
}

// 
static Task<void> handle_client(std::unique_ptr<Socket> sock) {
    try {
        TcpConnection conn(std::move(sock));
        std::cout << "New connection established" << std::endl;
        
        while (!conn.is_closed()) {
            // 
            std::string line = co_await conn.read_line();
            if (line.empty()) {
                std::cout << "Connection closed by client" << std::endl;
                break; // EOF
            }
            std::cout << "Received: " << line;
            // 
            co_await conn.write(line);
            co_await conn.flush();
        }
        conn.close();
        std::cout << "Connection handler completed normally" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Connection error: " << e.what() << std::endl;
    }
    co_return;
}

int main(int argc, char** argv) {
    std::string host = "0.0.0.0";
    uint16_t port = 12345;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    try {
        // 
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        // 
        auto& loop = GlobalEventLoop::get();

        // TCP
        TcpServer server(&loop);
        server.set_connection_handler(handle_client);

        std::cout << "Echo server listening on " << host << ":" << port << std::endl;

        // 
        sync_wait(server.listen(host, port));

        // 
        while (server_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 
        std::cout << "Stopping server..." << std::endl;
        server.stop();
        GlobalEventLoop::shutdown();
        
        std::cout << "Server stopped" << std::endl;
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
}
