/**
 * @file test_network_professional.cpp
 * @brief FlowCoro 
 * 
 * 
 * - 
 * - 
 * - 
 * - 
 * - 
 */

#include "flowcoro.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <random>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using namespace flowcoro;
using namespace flowcoro::net;
using namespace std::chrono;

class NetworkTester {
private:
    std::atomic<bool> server_running_{true};
    std::atomic<bool> server_ready_{false};
    std::atomic<size_t> connections_handled_{0};
    std::atomic<size_t> messages_processed_{0};
    std::atomic<size_t> bytes_transferred_{0};
    
    struct TestStats {
        size_t total_connections = 0;
        size_t successful_connections = 0;
        size_t failed_connections = 0;
        size_t total_messages = 0;
        size_t total_bytes = 0;
        double avg_latency_ms = 0;
        double throughput_mbps = 0;
        steady_clock::time_point start_time;
        steady_clock::time_point end_time;
    };

public:
    // Echo 
    Task<void> echo_handler(std::unique_ptr<Socket> socket) {
        try {
            TcpConnection conn(std::move(socket));
            connections_handled_.fetch_add(1);
            
            while (!conn.is_closed()) {
                auto data = co_await conn.read_line();
                if (data.empty()) break;
                
                messages_processed_.fetch_add(1);
                bytes_transferred_.fetch_add(data.size() * 2); // +
                
                co_await conn.write(data);
                co_await conn.flush();
            }
            
        } catch (const std::exception& e) {
            std::cout << "Connection error: " << e.what() << std::endl;
        }
        co_return;
    }

    // 
    Task<void> start_test_server(uint16_t port) {
        auto& loop = GlobalEventLoop::get();
        TcpServer server(&loop);
        
        server.set_connection_handler([this](std::unique_ptr<Socket> s) -> Task<void> {
            co_return co_await echo_handler(std::move(s));
        });
        
        std::cout << "Starting test server on port " << port << std::endl;
        try {
            co_await server.listen("127.0.0.1", port);
            std::cout << "Test server listening successfully on 127.0.0.1:" << port << std::endl;
            server_ready_.store(true);  // 
            
            // accept_loop
            co_await sleep_for(std::chrono::milliseconds(50));
            std::cout << "Test server accept loop started" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Failed to start server: " << e.what() << std::endl;
            co_return;
        }
        
        while (server_running_.load()) {
            co_await sleep_for(std::chrono::milliseconds(100));
        }
        
        server.stop();
        std::cout << "Test server stopped." << std::endl;
        co_return;
    }

    // 
    bool wait_for_server_ready(int timeout_ms = 5000) {
        auto start = steady_clock::now();
        while (!server_ready_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                std::cout << "Timeout waiting for server to be ready" << std::endl;
                return false;
            }
        }
        return true;
    }

    // TCP
    int create_client_socket(const std::string& host, uint16_t port) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            std::cout << "Failed to create socket: " << strerror(errno) << std::endl;
            return -1;
        }
        
        // 
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
        
        int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
        if (result == -1 && errno != EINPROGRESS) {
            std::cout << "Connect failed: " << strerror(errno) << std::endl;
            close(sock);
            return -1;
        }
        
        // 
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);
        
        timeval timeout{1, 0}; // 1
        int select_result = select(sock + 1, nullptr, &write_fds, nullptr, &timeout);
        if (select_result <= 0) {
            std::cout << "Connection timeout or error, select result: " << select_result << std::endl;
            close(sock);
            return -1;
        }
        
        // 
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) != 0 || error != 0) {
            close(sock);
            return -1;
        }
        
        return sock;
    }

    // 
    TestStats test_concurrent_connections(uint16_t port, size_t num_connections, size_t messages_per_conn) {
        TestStats stats;
        stats.start_time = steady_clock::now();
        
        std::vector<std::thread> client_threads;
        std::atomic<size_t> successful_conns{0};
        std::atomic<size_t> failed_conns{0};
        std::atomic<size_t> total_msgs{0};
        std::atomic<size_t> total_bytes_sent{0};
        
        std::cout << "Testing " << num_connections << " concurrent connections, " 
                  << messages_per_conn << " messages each" << std::endl;
        
        for (size_t i = 0; i < num_connections; ++i) {
            client_threads.emplace_back([=, &successful_conns, &failed_conns, &total_msgs, &total_bytes_sent]() {
                int sock = create_client_socket("127.0.0.1", port);
                if (sock == -1) {
                    failed_conns.fetch_add(1);
                    return;
                }
                
                successful_conns.fetch_add(1);
                
                for (size_t j = 0; j < messages_per_conn; ++j) {
                    std::string msg = "test_message_" + std::to_string(i) + "_" + std::to_string(j) + "\n";
                    
                    // 
                    ssize_t sent = send(sock, msg.c_str(), msg.size(), MSG_NOSIGNAL);
                    if (sent <= 0) break;
                    
                    // 
                    char buffer[1024];
                    ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
                    if (received <= 0) break;
                    
                    total_msgs.fetch_add(1);
                    total_bytes_sent.fetch_add(sent + received);
                }
                
                close(sock);
            });
        }
        
        // 
        for (auto& t : client_threads) {
            t.join();
        }
        
        stats.end_time = steady_clock::now();
        stats.total_connections = num_connections;
        stats.successful_connections = successful_conns.load();
        stats.failed_connections = failed_conns.load();
        stats.total_messages = total_msgs.load();
        stats.total_bytes = total_bytes_sent.load();
        
        auto duration = duration_cast<milliseconds>(stats.end_time - stats.start_time);
        stats.throughput_mbps = (stats.total_bytes * 8.0) / (duration.count() * 1000.0); // Mbps
        stats.avg_latency_ms = static_cast<double>(duration.count()) / stats.total_messages;
        
        return stats;
    }

    // 
    TestStats test_throughput(uint16_t port, size_t message_size, size_t num_messages) {
        TestStats stats;
        stats.start_time = steady_clock::now();
        
        int sock = create_client_socket("127.0.0.1", port);
        if (sock == -1) {
            std::cout << "Failed to create client socket for throughput test" << std::endl;
            return stats;
        }
        
        std::string large_msg(message_size, 'X');
        large_msg += "\n";
        
        std::cout << "Testing throughput with " << num_messages << " messages of " 
                  << message_size << " bytes each" << std::endl;
        
        size_t successful_msgs = 0;
        size_t total_bytes_sent = 0;
        
        for (size_t i = 0; i < num_messages; ++i) {
            // 
            ssize_t sent = send(sock, large_msg.c_str(), large_msg.size(), MSG_NOSIGNAL);
            if (sent <= 0) break;
            
            // 
            std::string received;
            char buffer[4096];
            while (received.size() < large_msg.size()) {
                ssize_t recv_bytes = recv(sock, buffer, sizeof(buffer), 0);
                if (recv_bytes <= 0) break;
                received.append(buffer, recv_bytes);
            }
            
            if (received.size() == large_msg.size()) {
                successful_msgs++;
                total_bytes_sent += sent + received.size();
            }
        }
        
        close(sock);
        
        stats.end_time = steady_clock::now();
        stats.total_messages = successful_msgs;
        stats.total_bytes = total_bytes_sent;
        
        auto duration = duration_cast<milliseconds>(stats.end_time - stats.start_time);
        stats.throughput_mbps = (stats.total_bytes * 8.0) / (duration.count() * 1000.0);
        stats.avg_latency_ms = static_cast<double>(duration.count()) / stats.total_messages;
        
        return stats;
    }

    // 
    TestStats test_long_connection_stability(uint16_t port, std::chrono::seconds duration) {
        TestStats stats;
        stats.start_time = steady_clock::now();
        
        int sock = create_client_socket("127.0.0.1", port);
        if (sock == -1) {
            std::cout << "Failed to create client socket for stability test" << std::endl;
            return stats;
        }
        
        std::cout << "Testing connection stability for " << duration.count() << " seconds" << std::endl;
        
        size_t message_count = 0;
        size_t total_bytes_sent = 0;
        auto end_time = steady_clock::now() + duration;
        
        while (steady_clock::now() < end_time) {
            std::string msg = "stability_test_" + std::to_string(message_count) + "\n";
            
            ssize_t sent = send(sock, msg.c_str(), msg.size(), MSG_NOSIGNAL);
            if (sent <= 0) break;
            
            char buffer[1024];
            ssize_t received = recv(sock, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            
            message_count++;
            total_bytes_sent += sent + received;
            
            // 
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        close(sock);
        
        stats.end_time = steady_clock::now();
        stats.total_messages = message_count;
        stats.total_bytes = total_bytes_sent;
        
        auto actual_duration = duration_cast<milliseconds>(stats.end_time - stats.start_time);
        stats.throughput_mbps = (stats.total_bytes * 8.0) / (actual_duration.count() * 1000.0);
        stats.avg_latency_ms = static_cast<double>(actual_duration.count()) / stats.total_messages;
        
        return stats;
    }

    void print_stats(const std::string& test_name, const TestStats& stats) {
        auto duration = duration_cast<milliseconds>(stats.end_time - stats.start_time);
        
        std::cout << "\n" << std::string(60, '=') << std::endl;
        std::cout << test_name << " Results:" << std::endl;
        std::cout << std::string(60, '-') << std::endl;
        std::cout << "Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "Total connections: " << stats.total_connections << std::endl;
        std::cout << "Successful connections: " << stats.successful_connections << std::endl;
        std::cout << "Failed connections: " << stats.failed_connections << std::endl;
        std::cout << "Total messages: " << stats.total_messages << std::endl;
        std::cout << "Total bytes: " << stats.total_bytes << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(2) 
                  << stats.throughput_mbps << " Mbps" << std::endl;
        std::cout << "Average latency: " << std::fixed << std::setprecision(3) 
                  << stats.avg_latency_ms << " ms/message" << std::endl;
        std::cout << "Server handled connections: " << connections_handled_.load() << std::endl;
        std::cout << "Server processed messages: " << messages_processed_.load() << std::endl;
        std::cout << std::string(60, '=') << std::endl;
    }

    void run_all_tests(uint16_t port) {
        // 
        connections_handled_ = 0;
        messages_processed_ = 0;
        bytes_transferred_ = 0;
        
        std::cout << "FlowCoro Network Module Professional Test Suite" << std::endl;
        std::cout << "================================================" << std::endl;
        
        // 
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 1: 
        auto test1 = test_concurrent_connections(port, 50, 10);
        print_stats("Concurrent Connections Test (50 conns, 10 msgs each)", test1);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 2: 
        auto test2 = test_concurrent_connections(port, 200, 5);
        print_stats("High Concurrency Test (200 conns, 5 msgs each)", test2);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 3: 
        auto test3 = test_throughput(port, 1024, 1000);
        print_stats("Throughput Test (1KB messages)", test3);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 4: 
        auto test4 = test_long_connection_stability(port, std::chrono::seconds(10));
        print_stats("Long Connection Stability Test (10s)", test4);
        
        server_running_ = false;
        
        std::cout << "\nAll tests completed!" << std::endl;
        
        // 
        std::cout << "\nOverall Server Performance:" << std::endl;
        std::cout << "- Total connections handled: " << connections_handled_.load() << std::endl;
        std::cout << "- Total messages processed: " << messages_processed_.load() << std::endl;
        std::cout << "- Total bytes transferred: " << bytes_transferred_.load() << std::endl;
    }
};

int main(int argc, char* argv[]) {
    // SIGPIPEsocket
    signal(SIGPIPE, SIG_IGN);
    
    uint16_t port = 18888;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    
    try {
        NetworkTester tester;
        
        // 
        auto& loop = GlobalEventLoop::get();  // EventLoop 
        auto& manager = flowcoro::CoroutineManager::get_instance();
        
        // 
        auto server_task = tester.start_test_server(port);
        
        // 
        auto server_handle = server_task.handle;
        if (server_handle && !server_handle.done()) {
            manager.schedule_resume(server_handle);
        }
        
        // 
        std::atomic<bool> should_stop{false};
        std::thread driver_thread([&manager, &should_stop]() {
            while (!should_stop.load()) {
                manager.drive();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        
        // 
        if (!tester.wait_for_server_ready()) {
            std::cerr << "Server failed to start within timeout" << std::endl;
            should_stop = true;
            loop.stop();
            driver_thread.join();
            return 1;
        }
        
        std::cout << "Server is ready, starting tests..." << std::endl;
        
        // 
        tester.run_all_tests(port);
        
        // 
        should_stop = true;
        loop.stop();
        driver_thread.join();
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test error: " << e.what() << std::endl;
        return 1;
    }
}
