/**
 * @file net.h
 * @brief FlowCoro 网络IO层 - 异步网络编程支持
 * @author FlowCoro Team
 * @version 2.1.0
 * @date 2025-07-10
 *
 * 提供基于epoll/kqueue的高性能异步网络IO能力:
 * - 事件驱动的异步IO
 * - TCP客户端和服务器
 * - HTTP协议支持
 * - WebSocket升级支持
 */

#pragma once

#include <sys/epoll.h>
#include <sys/socket.h>
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

#include "core.h"
#include "lockfree.h"

namespace flowcoro::net {

// 前向声明
class EventLoop;
class Socket;
class TcpServer;
class TcpConnection;

// IO事件类型
enum class IoEvent : uint32_t {
    READ = EPOLLIN,
    WRITE = EPOLLOUT,
    ERROR = EPOLLERR,
    HANGUP = EPOLLHUP,
    EDGE_TRIGGERED = EPOLLET
};

// IO事件回调
struct IoEventHandler {
    std::function<void()> on_read;
    std::function<void()> on_write;
    std::function<void()> on_error;
    int fd{-1};
    uint32_t events{0};
};

/**
 * @brief 高性能事件循环
 * 基于Linux epoll实现的异步IO事件循环
 */
class EventLoop {
private:
    int epoll_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, std::unique_ptr<IoEventHandler>> handlers_;
    lockfree::Queue<std::function<void()>> pending_tasks_;

    // 定时器支持
    struct TimerEvent {
        std::chrono::steady_clock::time_point when;
        std::function<void()> callback;
        bool operator<(const TimerEvent& other) const {
            return when > other.when; // 最小堆
        }
    };
    std::priority_queue<TimerEvent> timer_queue_;
    std::mutex timer_mutex_;

public:
    EventLoop();
    ~EventLoop();

    // 禁止拷贝
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /**
     * @brief 启动事件循环
     * @return 协程任务
     */
    Task<void> run();

    /**
     * @brief 停止事件循环
     */
    void stop();

    /**
     * @brief 添加文件描述符到事件循环
     * @param fd 文件描述符
     * @param events 监听的事件类型
     * @param handler 事件处理器
     */
    void add_fd(int fd, uint32_t events, std::unique_ptr<IoEventHandler> handler);

    /**
     * @brief 修改文件描述符的事件
     * @param fd 文件描述符
     * @param events 新的事件类型
     */
    void modify_fd(int fd, uint32_t events);

    /**
     * @brief 从事件循环中移除文件描述符
     * @param fd 文件描述符
     */
    void remove_fd(int fd);

    /**
     * @brief 在事件循环中执行任务
     * @param task 要执行的任务
     */
    void post_task(std::function<void()> task);

    /**
     * @brief 定时执行任务
     * @param delay 延迟时间
     * @param callback 回调函数
     */
    void schedule_timer(std::chrono::milliseconds delay, std::function<void()> callback);

    /**
     * @brief 检查是否在运行
     * @return 是否运行中
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

private:
    void process_pending_tasks();
    void process_timers();
    int get_next_timeout();
};

/**
 * @brief 异步Socket封装
 * 提供非阻塞Socket操作的协程接口
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

    // 禁止拷贝，允许移动
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    /**
     * @brief 连接到远程地址
     * @param host 主机地址
     * @param port 端口号
     * @return 协程任务
     */
    Task<void> connect(const std::string& host, uint16_t port);

    /**
     * @brief 绑定到本地地址
     * @param host 本地地址
     * @param port 端口号
     * @return 是否成功
     */
    bool bind(const std::string& host, uint16_t port);

    /**
     * @brief 开始监听连接
     * @param backlog 连接队列大小
     * @return 是否成功
     */
    bool listen(int backlog = 128);

    /**
     * @brief 接受新连接
     * @return 新连接的Socket
     */
    Task<std::unique_ptr<Socket>> accept();

    /**
     * @brief 异步读取数据
     * @param buffer 缓冲区
     * @param size 最大读取字节数
     * @return 实际读取的字节数
     */
    Task<ssize_t> read(char* buffer, size_t size);

    /**
     * @brief 异步写入数据
     * @param data 要写入的数据
     * @param size 数据大小
     * @return 实际写入的字节数
     */
    Task<ssize_t> write(const char* data, size_t size);

    /**
     * @brief 读取一行数据（以\n结尾）
     * @return 读取的字符串
     */
    Task<std::string> read_line();

    /**
     * @brief 读取指定长度的数据
     * @param size 要读取的字节数
     * @return 读取的数据
     */
    Task<std::string> read_exactly(size_t size);

    /**
     * @brief 写入字符串
     * @param data 要写入的字符串
     * @return 写入的字节数
     */
    Task<size_t> write_string(const std::string& data);

    /**
     * @brief 关闭Socket
     */
    void close();

    /**
     * @brief 获取文件描述符
     * @return 文件描述符
     */
    int fd() const { return fd_; }

    /**
     * @brief 检查是否已连接
     * @return 是否已连接
     */
    bool is_connected() const { return connected_; }

    /**
     * @brief 设置Socket选项
     * @param option 选项名
     * @param value 选项值
     */
    void set_option(int option, int value);

private:
    void make_non_blocking();
    sockaddr_in create_address(const std::string& host, uint16_t port);
};

/**
 * @brief TCP服务器
 * 高性能异步TCP服务器实现
 */
class TcpServer {
private:
    EventLoop* loop_;
    std::unique_ptr<Socket> listen_socket_;
    std::function<Task<void>(std::unique_ptr<Socket>)> connection_handler_;
    std::atomic<bool> running_{false};

public:
    explicit TcpServer(EventLoop* loop);
    ~TcpServer();

    /**
     * @brief 设置连接处理器
     * @param handler 连接处理函数
     */
    template<typename Handler>
    void set_connection_handler(Handler&& handler) {
        connection_handler_ = std::forward<Handler>(handler);
    }

    /**
     * @brief 开始监听
     * @param host 监听地址
     * @param port 监听端口
     * @return 协程任务
     */
    Task<void> listen(const std::string& host, uint16_t port);

    /**
     * @brief 停止服务器
     */
    void stop();

    /**
     * @brief 检查是否在运行
     * @return 是否运行中
     */
    bool is_running() const { return running_.load(std::memory_order_acquire); }

private:
    Task<void> accept_loop();
};

/**
 * @brief TCP连接
 * TCP连接的高级封装，提供缓冲读写
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
     * @brief 读取一行
     * @return 读取的字符串
     */
    Task<std::string> read_line();

    /**
     * @brief 读取指定长度数据
     * @param size 要读取的字节数
     * @return 读取的数据
     */
    Task<std::string> read(size_t size);

    /**
     * @brief 写入数据
     * @param data 要写入的数据
     * @return 协程任务
     */
    Task<void> write(const std::string& data);

    /**
     * @brief 刷新写缓冲区
     * @return 协程任务
     */
    Task<void> flush();

    /**
     * @brief 关闭连接
     */
    void close();

    /**
     * @brief 检查连接是否关闭
     * @return 是否已关闭
     */
    bool is_closed() const { return closed_; }

    /**
     * @brief 获取底层Socket
     * @return Socket指针
     */
    Socket* socket() { return socket_.get(); }
};

// 全局事件循环实例
class GlobalEventLoop {
private:
    static std::unique_ptr<EventLoop> instance_;
    static std::once_flag init_flag_;

public:
    static void initialize() {
        std::call_once(init_flag_, []() {
            instance_ = std::make_unique<EventLoop>();
        });
    }

    static EventLoop& get() {
        initialize();
        return *instance_;
    }

    static void shutdown() {
        if (instance_) {
            instance_->stop();
            instance_.reset();
        }
    }
};

} // namespace flowcoro::net
