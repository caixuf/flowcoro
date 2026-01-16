/**
 * @file net.h
 * @brief FlowCoro IO - 
 * @author FlowCoro Team
 * @version 2.1.0
 * @date 2025-07-10
 *
 * epoll/kqueueIO:
 * - IO
 * - TCP
 * - HTTP
 * - WebSocket
 */

#pragma once

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <chrono>
#include <queue>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>

#include "core.h"
#include "lockfree.h"

namespace flowcoro::net {

// 
class EventLoop;
class Socket;
class TcpServer;
class TcpConnection;

// IO
enum class IoEvent : uint32_t {
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
    ERROR = EPOLLERR,
    HANGUP = EPOLLHUP,
    EDGE_TRIGGERED = EPOLLET
};

// IO
struct IoEventHandler {
    std::function<void()> on_read;
    std::function<void()> on_write;
    std::function<void()> on_error;
    int fd{-1};
    uint32_t events{0};
};

/**
 * @brief 
 * Linux epollIO
 */
class EventLoop {
private:
    int epoll_fd_{-1};
    int wakeup_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, std::unique_ptr<IoEventHandler>> handlers_;
    //  handlers_ /UAF
    std::mutex handlers_mutex_;
    lockfree::Queue<std::function<void()>> pending_tasks_;

    // 
    struct TimerEvent {
        std::chrono::steady_clock::time_point when;
        std::function<void()> callback;
        bool operator<(const TimerEvent& other) const {
            return when > other.when; // 
        }
    };
    std::priority_queue<TimerEvent> timer_queue_;
    std::mutex timer_mutex_;

    // 
    std::thread event_thread_;
    
    // 
    void run_loop();

public:
    EventLoop();
    ~EventLoop();

    // 
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /**
     * @brief 
     */
    void start();

    /**
     * @brief 
     */
    void stop();
    
    /**
     * @brief 
     */
    void wait_for_stop();

    /**
     * @brief 
     * @param fd 
     * @param events 
     * @param handler 
     */
    void add_fd(int fd, uint32_t events, std::unique_ptr<IoEventHandler> handler);

    /**
     * @brief 
     * @param fd 
     * @param events 
     */
    void modify_fd(int fd, uint32_t events);

    /**
     * @brief 
     * @param fd 
     */
    void remove_fd(int fd);

    /**
     * @brief 
     * @param task 
     */
    void post_task(std::function<void()> task);

    /**
     * @brief 
     * @param delay 
     * @param callback 
     */
    void schedule_timer(std::chrono::milliseconds delay, std::function<void()> callback);

    /**
     * @brief 
     * @return 
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

private:
    void process_pending_tasks();
    void process_timers();
    int get_next_timeout();
};

/**
 * @brief Socket
 * Socket
 */
class Socket {
private:
    int fd_{-1};
    EventLoop* loop_{nullptr};
    bool connected_{false};

public:
    explicit Socket(EventLoop* loop);
    Socket(int fd, EventLoop* loop);
    ~Socket();

    // 
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    /**
     * @brief 
     * @param host 
     * @param port 
     * @return 
     */
    Task<void> connect(const std::string& host, uint16_t port);

    /**
     * @brief 
     * @param host 
     * @param port 
     * @return 
     */
    bool bind(const std::string& host, uint16_t port);

    /**
     * @brief 
     * @param backlog 
     * @return 
     */
    bool listen(int backlog = 128);

    /**
     * @brief 
     * @return Socket
     */
    Task<std::unique_ptr<Socket>> accept();

    /**
     * @brief 
     * @param buffer 
     * @param size 
     * @return 
     */
    Task<ssize_t> read(char* buffer, size_t size);

    /**
     * @brief 
     * @param data 
     * @param size 
     * @return 
     */
    Task<ssize_t> write(const char* data, size_t size);

    /**
     * @brief \n
     * @return 
     */
    Task<std::string> read_line();

    /**
     * @brief 
     * @param size 
     * @return 
     */
    Task<std::string> read_exactly(size_t size);

    /**
     * @brief 
     * @param data 
     * @return 
     */
    Task<size_t> write_string(const std::string& data);

    /**
     * @brief Socket
     */
    void close();

    /**
     * @brief 
     * @return 
     */
    int fd() const { return fd_; }

    /**
     * @brief 
     * @return 
     */
    bool is_connected() const { return connected_; }

    /**
     * @brief Socket
     * @param option 
     * @param value 
     */
    void set_option(int option, int value);

private:
    void make_non_blocking();
    sockaddr_in create_address(const std::string& host, uint16_t port);
};

/**
 * @brief TCP
 * TCP
 */
class TcpServer {
private:
    EventLoop* loop_;
    std::unique_ptr<Socket> listen_socket_;
    std::function<Task<void>(std::unique_ptr<Socket>)> connection_handler_;
    std::atomic<bool> running_{false};
    // accept_looplisten
    std::optional<Task<void>> accept_task_;
    // 
    std::mutex active_tasks_mutex_;
    std::vector<std::shared_ptr<Task<void>>> active_tasks_;

public:
    explicit TcpServer(EventLoop* loop);
    ~TcpServer();

    /**
     * @brief 
     * @param handler 
     */
    template<typename Handler>
    void set_connection_handler(Handler&& handler) {
        connection_handler_ = std::forward<Handler>(handler);
    }

    /**
     * @brief 
     * @param host 
     * @param port 
     * @return 
     */
    Task<void> listen(const std::string& host, uint16_t port);

    /**
     * @brief 
     */
    void stop();

    /**
     * @brief 
     * @return 
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

private:
    Task<void> accept_loop();
};

/**
 * @brief TCP
 * TCP
 */
class TcpConnection {
private:
    std::unique_ptr<Socket> socket_;
    std::string read_buffer_;
    std::string write_buffer_;
    bool closed_{false};

public:
    explicit TcpConnection(std::unique_ptr<Socket> socket);
    ~TcpConnection();

    /**
     * @brief 
     * @return 
     */
    Task<std::string> read_line();

    /**
     * @brief 
     * @param size 
     * @return 
     */
    Task<std::string> read(size_t size);

    /**
     * @brief 
     * @param data 
     * @return 
     */
    Task<void> write(const std::string& data);

    /**
     * @brief 
     * @return 
     */
    Task<void> flush();

    /**
     * @brief 
     */
    void close();

    /**
     * @brief 
     * @return 
     */
    bool is_closed() const { return closed_; }

    /**
     * @brief Socket
     * @return Socket
     */
    Socket* socket() { return socket_.get(); }
};

// 
class GlobalEventLoop {
private:
    static std::unique_ptr<EventLoop> instance_;
    static std::once_flag init_flag_;

public:
    static void initialize() {
        std::call_once(init_flag_, []() {
            instance_ = std::make_unique<EventLoop>();
            instance_->start(); // 
        });
    }

    static EventLoop& get() {
        initialize();
        return *instance_;
    }
    
    static void shutdown() {
        if (instance_) {
            instance_->stop();
            instance_->wait_for_stop();
            instance_.reset();
        }
    }
};

} // namespace flowcoro::net
