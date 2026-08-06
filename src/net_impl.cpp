/**
 * @file net_impl.cpp
 * @brief FlowCoro 网络IO层实现（跨平台：Linux epoll / Windows WSAPoll）
 */

#include "flowcoro/net.h"
#include "flowcoro/logger.h"
#include <stdexcept>
#include <algorithm>
#include <cerrno>
#include <thread>

#ifndef _WIN32
#include <sys/eventfd.h>
#endif

namespace flowcoro::net {

// ============================================================================
// 平台适配辅助（socket 错误码、非阻塞标志、收发封装等）
// ============================================================================
namespace {

#ifdef _WIN32

// Winsock 全局初始化（进程级，引用计数安全，可与 http_client 的初始化共存）
struct WsaInitializer {
    WsaInitializer() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WsaInitializer() {
        WSACleanup();
    }
};
static WsaInitializer g_wsa_initializer;

inline int last_socket_error() { return WSAGetLastError(); }
inline bool err_would_block(int e) { return e == WSAEWOULDBLOCK; }
inline bool err_in_progress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
inline void close_socket(socket_t s) { ::closesocket(s); }

inline std::string socket_error_string(int e) {
    char* buffer = nullptr;
    DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(e),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer), 0, nullptr);
    std::string msg = (len && buffer) ? std::string(buffer, len) : ("error " + std::to_string(e));
    if (buffer) LocalFree(buffer);
    // 去掉尾部换行
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
    return msg;
}

inline ssize_t sock_recv(socket_t s, char* buf, size_t len) {
    return ::recv(s, buf, static_cast<int>(len), 0);
}
inline ssize_t sock_send(socket_t s, const char* buf, size_t len) {
    return ::send(s, buf, static_cast<int>(len), 0);
}

inline void set_non_blocking(socket_t s) {
    u_long mode = 1;
    if (::ioctlsocket(s, FIONBIO, &mode) != 0) {
        throw std::runtime_error("Set non-blocking failed: " +
                                 socket_error_string(last_socket_error()));
    }
}

#else

inline int last_socket_error() { return errno; }
inline bool err_would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
inline bool err_in_progress(int e) { return e == EINPROGRESS; }
inline void close_socket(socket_t s) { ::close(s); }
inline std::string socket_error_string(int e) { return std::string(std::strerror(e)); }

inline ssize_t sock_recv(socket_t s, char* buf, size_t len) {
    return ::recv(s, buf, len, 0);
}
inline ssize_t sock_send(socket_t s, const char* buf, size_t len) {
    return ::send(s, buf, len, MSG_NOSIGNAL);
}

inline void set_non_blocking(socket_t s) {
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("Get socket flags failed: " +
                                 socket_error_string(last_socket_error()));
    }
    if (::fcntl(s, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error("Set non-blocking failed: " +
                                 socket_error_string(last_socket_error()));
    }
}

#endif

// 跨平台非阻塞 accept：返回一个已设为非阻塞的已连接套接字，失败返回 INVALID_SOCKET_HANDLE
inline socket_t accept_non_blocking(socket_t listen_fd, sockaddr* addr, socklen_t* addr_len) {
#if defined(__linux__)
    return ::accept4(listen_fd, addr, addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    socket_t s = ::accept(listen_fd, addr, addr_len);
    if (s != INVALID_SOCKET_HANDLE) {
        try {
            set_non_blocking(s);
        } catch (...) {
            close_socket(s);
            return INVALID_SOCKET_HANDLE;
        }
    }
    return s;
#endif
}

} // namespace

// 静态成员定义
std::unique_ptr<EventLoop> GlobalEventLoop::instance_;
std::once_flag GlobalEventLoop::init_flag_;

// ============================================================================
// EventLoop 实现
// ============================================================================

#ifdef _WIN32
namespace {
// 在 loopback 上创建一对已连接的套接字，用作事件循环的自唤醒管道。
// 返回 true 表示成功；recv_end 用于被 WSAPoll 监听，send_end 用于写入唤醒字节。
bool make_wakeup_pair(socket_t& recv_end, socket_t& send_end) {
    recv_end = INVALID_SOCKET_HANDLE;
    send_end = INVALID_SOCKET_HANDLE;

    socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET_HANDLE) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // 让系统分配端口

    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(listener, 1) != 0) {
        ::closesocket(listener);
        return false;
    }

    int addr_len = sizeof(addr);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0) {
        ::closesocket(listener);
        return false;
    }

    socket_t client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET_HANDLE) {
        ::closesocket(listener);
        return false;
    }

    if (::connect(client, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(listener);
        ::closesocket(client);
        return false;
    }

    socket_t server = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    if (server == INVALID_SOCKET_HANDLE) {
        ::closesocket(client);
        return false;
    }

    // recv 端设置为非阻塞，便于在唤醒后一次性排空
    u_long mode = 1;
    ::ioctlsocket(server, FIONBIO, &mode);

    recv_end = server;
    send_end = client;
    return true;
}
} // namespace

EventLoop::EventLoop() {
    if (!make_wakeup_pair(wakeup_recv_, wakeup_send_)) {
        throw std::runtime_error("Failed to create wakeup socket pair: " +
                                 socket_error_string(last_socket_error()));
    }
}

EventLoop::~EventLoop() {
    stop();
    wait_for_stop();
    if (wakeup_recv_ != INVALID_SOCKET_HANDLE) {
        close_socket(wakeup_recv_);
    }
    if (wakeup_send_ != INVALID_SOCKET_HANDLE) {
        close_socket(wakeup_send_);
    }
}

void EventLoop::wakeup() {
    if (wakeup_send_ != INVALID_SOCKET_HANDLE) {
        const char byte = 1;
        (void)::send(wakeup_send_, &byte, 1, 0);
    }
}

#else // POSIX / Linux

EventLoop::EventLoop() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll fd: " + std::string(strerror(errno)));
    }

    // 创建eventfd用于唤醒epoll_wait
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeup_fd_ == -1) {
        throw std::runtime_error("Failed to create eventfd: " + std::string(strerror(errno)));
    }

    // 将wakeup_fd添加到epoll监听
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) == -1) {
        ::close(wakeup_fd_);
        wakeup_fd_ = -1;
        throw std::runtime_error("Failed to add eventfd to epoll: " + std::string(strerror(errno)));
    }
}

EventLoop::~EventLoop() {
    stop();
    wait_for_stop();
    if (epoll_fd_ != -1) {
        ::close(epoll_fd_);
    }
    if (wakeup_fd_ != -1) {
        ::close(wakeup_fd_);
    }
}

void EventLoop::wakeup() {
    if (wakeup_fd_ != -1) {
        eventfd_t inc = 1;
        (void)eventfd_write(wakeup_fd_, inc);
    }
}

#endif

void EventLoop::start() {
    if (running_.load(std::memory_order_acquire)) {
        return; // 已经在运行
    }
    
    running_.store(true, std::memory_order_release);
    event_thread_ = std::thread(&EventLoop::run_loop, this);
}

void EventLoop::stop() {
    running_.store(false, std::memory_order_release);
    // 唤醒事件循环让它立即退出
    wakeup();
}

void EventLoop::wait_for_stop() {
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

void EventLoop::handle_io_event(socket_t fd, uint32_t ready_events) {
    std::function<void()> on_read_cb;
    std::function<void()> on_write_cb;
    std::function<void()> on_error_cb;

    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto handler_it = handlers_.find(fd);
        if (handler_it == handlers_.end()) {
            return; // 处理器已被移除
        }
        // 复制回调，避免回调中修改handlers_引发悬垂
        on_read_cb = handler_it->second->on_read;
        on_write_cb = handler_it->second->on_write;
        on_error_cb = handler_it->second->on_error;
    }

    // 处理错误和挂断事件
    if (ready_events & (static_cast<uint32_t>(IoEvent::ERROR) |
                        static_cast<uint32_t>(IoEvent::HANGUP))) {
        if (on_error_cb) {
            on_error_cb();
        }
        return;
    }

    // 处理读事件
    if (ready_events & static_cast<uint32_t>(IoEvent::READ)) {
        if (on_read_cb) {
            on_read_cb();
        }
    }

    // 处理写事件
    if (ready_events & static_cast<uint32_t>(IoEvent::WRITE)) {
        if (on_write_cb) {
            on_write_cb();
        }
    }
}

#ifdef _WIN32

void EventLoop::run_loop() {
    std::vector<WSAPOLLFD> pfds;

    while (running_.load(std::memory_order_acquire)) {
        // 处理定时器和待执行任务
        process_timers();
        process_pending_tasks();

        // 获取下次超时时间
        int timeout = get_next_timeout();

        // 根据当前 handlers_ 重建 WSAPoll 数组（含唤醒套接字）
        pfds.clear();
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            pfds.reserve(handlers_.size() + 1);

            WSAPOLLFD wake{};
            wake.fd = wakeup_recv_;
            wake.events = POLLRDNORM;
            pfds.push_back(wake);

            for (const auto& [fd, handler] : handlers_) {
                WSAPOLLFD pfd{};
                pfd.fd = fd;
                pfd.events = 0;
                if (handler->events & static_cast<uint32_t>(IoEvent::READ)) {
                    pfd.events |= POLLRDNORM;
                }
                if (handler->events & static_cast<uint32_t>(IoEvent::WRITE)) {
                    pfd.events |= POLLWRNORM;
                }
                pfds.push_back(pfd);
            }
        }

        int event_count = ::WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), timeout);

        if (event_count == SOCKET_ERROR) {
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            continue; // 忽略瞬时错误，重试
        }

        if (event_count == 0) {
            continue; // 超时，回到循环处理定时器/任务
        }

        for (const auto& pfd : pfds) {
            if (pfd.revents == 0) {
                continue;
            }

            // 处理唤醒事件：排空唤醒套接字
            if (pfd.fd == wakeup_recv_) {
                char buf[64];
                while (::recv(wakeup_recv_, buf, sizeof(buf), 0) > 0) {
                    // 读取直到为空
                }
                continue;
            }

            uint32_t ready = 0;
            if (pfd.revents & (POLLRDNORM | POLLIN)) {
                ready |= static_cast<uint32_t>(IoEvent::READ);
            }
            if (pfd.revents & (POLLWRNORM | POLLOUT)) {
                ready |= static_cast<uint32_t>(IoEvent::WRITE);
            }
            if (pfd.revents & POLLERR) {
                ready |= static_cast<uint32_t>(IoEvent::ERROR);
            }
            if (pfd.revents & (POLLHUP | POLLNVAL)) {
                ready |= static_cast<uint32_t>(IoEvent::HANGUP);
            }

            handle_io_event(pfd.fd, ready);
        }
    }
}

#else // POSIX / Linux

void EventLoop::run_loop() {
    const int max_events = 1024;
    epoll_event events[max_events];

    while (running_.load(std::memory_order_acquire)) {
        // 处理定时器和待执行任务
        process_timers();
        process_pending_tasks();

        // 获取下次超时时间
        int timeout = get_next_timeout();

        // 等待IO事件
        int event_count = epoll_wait(epoll_fd_, events, max_events, timeout);

        if (event_count == -1) {
            if (errno == EINTR) {
                continue; // 被信号中断，继续循环
            }
            // 如果不是在运行状态，正常退出
            if (!running_.load(std::memory_order_acquire)) {
                break;
            }
            throw std::runtime_error("epoll_wait failed: " + std::string(strerror(errno)));
        }

        // 处理IO事件
        for (int i = 0; i < event_count; ++i) {
            const auto& event = events[i];
            int fd = event.data.fd;

            // 处理唤醒事件：清空eventfd计数
            if (fd == wakeup_fd_) {
                eventfd_t val;
                while (eventfd_read(wakeup_fd_, &val) == 0) {
                    // 读取直到为空
                }
                continue;
            }

            uint32_t ready = 0;
            if (event.events & EPOLLIN) {
                ready |= static_cast<uint32_t>(IoEvent::READ);
            }
            if (event.events & EPOLLOUT) {
                ready |= static_cast<uint32_t>(IoEvent::WRITE);
            }
            if (event.events & EPOLLERR) {
                ready |= static_cast<uint32_t>(IoEvent::ERROR);
            }
            if (event.events & EPOLLHUP) {
                ready |= static_cast<uint32_t>(IoEvent::HANGUP);
            }

            handle_io_event(fd, ready);
        }
    }
}

#endif

#ifdef _WIN32

void EventLoop::add_fd(socket_t fd, uint32_t events, std::unique_ptr<IoEventHandler> handler) {
    handler->fd = fd;
    handler->events = events;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[fd] = std::move(handler);
    }
    // 唤醒事件循环，使其立即以新的 fd 集合重建 WSAPoll 数组
    wakeup();
}

void EventLoop::modify_fd(socket_t fd, uint32_t events) {
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto handler_it = handlers_.find(fd);
        if (handler_it != handlers_.end()) {
            handler_it->second->events = events;
        }
    }
    wakeup();
}

void EventLoop::remove_fd(socket_t fd) {
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_.erase(fd);
    }
    wakeup();
}

#else // POSIX / Linux

void EventLoop::add_fd(socket_t fd, uint32_t events, std::unique_ptr<IoEventHandler> handler) {
    epoll_event event{};
    event.events = 0;
    if (events & static_cast<uint32_t>(IoEvent::READ))  event.events |= EPOLLIN;
    if (events & static_cast<uint32_t>(IoEvent::WRITE)) event.events |= EPOLLOUT;
    if (events & static_cast<uint32_t>(IoEvent::EDGE_TRIGGERED)) event.events |= EPOLLET;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        throw std::runtime_error("Failed to add fd to epoll: " + std::string(strerror(errno)));
    }

    handler->fd = fd;
    handler->events = events;
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        handlers_[fd] = std::move(handler);
    }
}

void EventLoop::modify_fd(socket_t fd, uint32_t events) {
    epoll_event event{};
    event.events = 0;
    if (events & static_cast<uint32_t>(IoEvent::READ))  event.events |= EPOLLIN;
    if (events & static_cast<uint32_t>(IoEvent::WRITE)) event.events |= EPOLLOUT;
    if (events & static_cast<uint32_t>(IoEvent::EDGE_TRIGGERED)) event.events |= EPOLLET;
    event.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        throw std::runtime_error("Failed to modify fd in epoll: " + std::string(strerror(errno)));
    }

    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto handler_it = handlers_.find(fd);
    if (handler_it != handlers_.end()) {
        handler_it->second->events = events;
    }
}

void EventLoop::remove_fd(socket_t fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        // 可能fd已经关闭，这里不抛异常
    }

    std::lock_guard<std::mutex> lock(handlers_mutex_);
    handlers_.erase(fd);
}

#endif

void EventLoop::post_task(std::function<void()> task) {
    pending_tasks_.enqueue(std::move(task));
    // 唤醒事件循环
    wakeup();
}

void EventLoop::schedule_timer(std::chrono::milliseconds delay, std::function<void()> callback) {
    auto when = std::chrono::steady_clock::now() + delay;

    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push({when, std::move(callback)});
    }

    // 唤醒事件循环，让它重新计算超时时间
    wakeup();
}

void EventLoop::process_pending_tasks() {
    std::function<void()> task;
    int processed = 0;
    const int max_process = 100; // 避免长时间阻塞

    while (processed < max_process && pending_tasks_.dequeue(task)) {
        try {
            task();
        } catch (const std::exception& e) {
            // 记录异常但不中断事件循环
            LOG_ERROR("EventLoop pending task threw exception: %s", e.what());
        }
        ++processed;
    }
}

void EventLoop::process_timers() {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(timer_mutex_);
    while (!timer_queue_.empty() && timer_queue_.top().when <= now) {
        auto timer = timer_queue_.top();
        timer_queue_.pop();

        try {
            timer.callback();
        } catch (const std::exception& e) {
            // 记录异常但不中断事件循环
        }
    }
}

int EventLoop::get_next_timeout() {
    std::lock_guard<std::mutex> lock(timer_mutex_);

    if (timer_queue_.empty()) {
    return 10; // 更低的默认超时提升响应性
    }

    auto now = std::chrono::steady_clock::now();
    auto next_timeout = timer_queue_.top().when;

    if (next_timeout <= now) {
        return 0; // 立即超时
    }

    auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        next_timeout - now).count();

    return std::min(static_cast<int>(timeout_ms), 10);
}

// ============================================================================
// Socket 实现
// ============================================================================

Socket::Socket(EventLoop* loop) : loop_(loop) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == INVALID_SOCKET_HANDLE) {
        throw std::runtime_error("Failed to create socket: " +
                                 socket_error_string(last_socket_error()));
    }
    make_non_blocking();
}

Socket::Socket(socket_t fd, EventLoop* loop) : fd_(fd), loop_(loop), connected_(true) {
    if (fd_ == INVALID_SOCKET_HANDLE) {
        throw std::invalid_argument("Invalid file descriptor");
    }
    make_non_blocking();
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_), loop_(other.loop_), connected_(other.connected_) {
    other.fd_ = INVALID_SOCKET_HANDLE;
    other.loop_ = nullptr;
    other.connected_ = false;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        loop_ = other.loop_;
        connected_ = other.connected_;
        other.fd_ = INVALID_SOCKET_HANDLE;
        other.loop_ = nullptr;
        other.connected_ = false;
    }
    return *this;
}

Task<void> Socket::connect(const std::string& host, uint16_t port) {
    auto addr = create_address(host, port);

    int result = ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    if (result == 0) {
        connected_ = true;
        co_return;
    }

    if (!err_in_progress(last_socket_error())) {
        throw std::runtime_error("Connect failed: " +
                                 socket_error_string(last_socket_error()));
    }

    // 等待连接完成
    AsyncPromise<void> connect_promise;

    auto handler = std::make_unique<IoEventHandler>();
    handler->on_write = [&connect_promise, this]() {
        // 检查连接状态
        int error = 0;
        socklen_t len = sizeof(error);
        if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR,
                         reinterpret_cast<char*>(&error), &len) == 0 && error == 0) {
            connected_ = true;
            connect_promise.set_value();
        } else {
            connect_promise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error("Connect failed: " + socket_error_string(error))
                )
            );
        }
        loop_->remove_fd(fd_);
    };

    handler->on_error = [&connect_promise, this]() {
        connect_promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Connect error"))
        );
        loop_->remove_fd(fd_);
    };

    loop_->add_fd(fd_, static_cast<uint32_t>(IoEvent::WRITE), std::move(handler));

    co_await connect_promise;
}

bool Socket::bind(const std::string& host, uint16_t port) {
    auto addr = create_address(host, port);

    // 设置地址重用
    set_option(SO_REUSEADDR, 1);

    return ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
}

bool Socket::listen(int backlog) {
    return ::listen(fd_, backlog) == 0;
}

Task<std::unique_ptr<Socket>> Socket::accept() {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    socket_t client_fd = accept_non_blocking(
        fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

    if (client_fd != INVALID_SOCKET_HANDLE) {
        auto client_socket = std::make_unique<Socket>(client_fd, loop_);
        co_return std::move(client_socket);
    }

    if (!err_would_block(last_socket_error())) {
        throw std::runtime_error("Accept failed: " +
                                 socket_error_string(last_socket_error()));
    }

    // 等待新连接
    AsyncPromise<std::unique_ptr<Socket>> accept_promise;

    auto handler = std::make_unique<IoEventHandler>();
    handler->on_read = [&accept_promise, this]() {
        sockaddr_in inner_addr{};
        socklen_t inner_len = sizeof(inner_addr);

        socket_t inner_fd = accept_non_blocking(
            fd_, reinterpret_cast<sockaddr*>(&inner_addr), &inner_len);

        if (inner_fd != INVALID_SOCKET_HANDLE) {
            auto client_socket = std::make_unique<Socket>(inner_fd, loop_);
            accept_promise.set_value(std::move(client_socket));
        } else {
            accept_promise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error("Accept failed: " +
                                       socket_error_string(last_socket_error()))
                )
            );
        }

        loop_->remove_fd(fd_);
    };

    handler->on_error = [&accept_promise, this]() {
        accept_promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Accept error"))
        );
        loop_->remove_fd(fd_);
    };

    loop_->add_fd(fd_, static_cast<uint32_t>(IoEvent::READ), std::move(handler));

    co_return co_await accept_promise;
}

Task<ssize_t> Socket::read(char* buffer, size_t size) {
    ssize_t result = sock_recv(fd_, buffer, size);

    if (result > 0) {
        co_return result;
    }

    if (result == 0) {
        co_return 0; // EOF
    }

    if (!err_would_block(last_socket_error())) {
        throw std::runtime_error("Read failed: " +
                                 socket_error_string(last_socket_error()));
    }

    // 等待数据可读
    AsyncPromise<ssize_t> read_promise;

    auto handler = std::make_unique<IoEventHandler>();
    handler->on_read = [&read_promise, this, buffer, size]() {
        ssize_t total = 0;
        while (true) {
            ssize_t n = sock_recv(fd_, buffer + total, size - total);
            if (n > 0) {
                total += n;
                if (static_cast<size_t>(total) == size) {
                    break; // 满了
                }
                // 边沿触发下继续尝试，水平触发下也可提前返回
                continue;
            }
            if (n == 0) {
                // EOF
                break;
            }
            if (err_would_block(last_socket_error())) {
                break; // 暂无更多可读
            }
            // 错误
            read_promise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error("Read failed: " +
                                       socket_error_string(last_socket_error()))
                )
            );
            loop_->remove_fd(fd_);
            return;
        }
        read_promise.set_value(total);
        loop_->remove_fd(fd_);
    };

    handler->on_error = [&read_promise, this]() {
        read_promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Read error"))
        );
        loop_->remove_fd(fd_);
    };

    loop_->add_fd(fd_, static_cast<uint32_t>(IoEvent::READ), std::move(handler));

    co_return co_await read_promise;
}

Task<ssize_t> Socket::write(const char* data, size_t size) {
    // 使用 send()（POSIX 下设置 MSG_NOSIGNAL 避免 SIGPIPE，Windows 下无此标志）
    ssize_t result = sock_send(fd_, data, size);

    if (result > 0) {
        co_return result;
    }

    if (!err_would_block(last_socket_error())) {
        throw std::runtime_error("Write failed: " +
                                 socket_error_string(last_socket_error()));
    }

    // 等待可写
    AsyncPromise<ssize_t> write_promise;

    auto handler = std::make_unique<IoEventHandler>();
    handler->on_write = [&write_promise, this, data, size]() {
        ssize_t total = 0;
        while (true) {
            ssize_t n = sock_send(fd_, data + total, size - total);
            if (n > 0) {
                total += n;
                if (static_cast<size_t>(total) == size) {
                    break; // 全部写完
                }
                continue; // 边沿触发下继续写
            }
            if (n < 0 && err_would_block(last_socket_error())) {
                break; // 内核发送缓冲区已满，等待下次可写事件触发后继续
            }
            // 错误
            write_promise.set_exception(
                std::make_exception_ptr(
                    std::runtime_error("Write failed: " +
                                       socket_error_string(last_socket_error()))
                )
            );
            loop_->remove_fd(fd_);
            return;
        }
        write_promise.set_value(total);
        loop_->remove_fd(fd_);
    };

    handler->on_error = [&write_promise, this]() {
        write_promise.set_exception(
            std::make_exception_ptr(std::runtime_error("Write error"))
        );
        loop_->remove_fd(fd_);
    };

    loop_->add_fd(fd_, static_cast<uint32_t>(IoEvent::WRITE), std::move(handler));

    co_return co_await write_promise;
}

Task<std::string> Socket::read_line() {
    std::string line;
    char ch;

    while (true) {
        ssize_t n = co_await read(&ch, 1);
        if (n == 0) {
            break; // EOF
        }
        if (n < 0) {
            throw std::runtime_error("Read failed");
        }

        line += ch;
        if (ch == '\n') {
            break;
        }
    }

    co_return line;
}

Task<std::string> Socket::read_exactly(size_t size) {
    std::string data;
    data.reserve(size);

    while (data.size() < size) {
        char buffer[4096];
        size_t to_read = std::min(sizeof(buffer), size - data.size());

        ssize_t n = co_await read(buffer, to_read);
        if (n == 0) {
            break; // EOF
        }
        if (n < 0) {
            throw std::runtime_error("Read failed");
        }

        data.append(buffer, n);
    }

    co_return data;
}

Task<size_t> Socket::write_string(const std::string& data) {
    size_t total_written = 0;

    while (total_written < data.size()) {
        ssize_t n = co_await write(data.data() + total_written,
                                  data.size() - total_written);
        if (n <= 0) {
            throw std::runtime_error("Write failed");
        }
        total_written += n;
    }

    co_return total_written;
}

void Socket::close() {
    if (fd_ != INVALID_SOCKET_HANDLE) {
        if (loop_) {
            loop_->remove_fd(fd_);
        }
        close_socket(fd_);
        fd_ = INVALID_SOCKET_HANDLE;
        connected_ = false;
    }
}

void Socket::set_option(int option, int value) {
    if (::setsockopt(fd_, SOL_SOCKET, option,
                     reinterpret_cast<const char*>(&value), sizeof(value)) != 0) {
        throw std::runtime_error("Set socket option failed: " +
                                 socket_error_string(last_socket_error()));
    }
}

void Socket::make_non_blocking() {
    set_non_blocking(fd_);
}

sockaddr_in Socket::create_address(const std::string& host, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host == "0.0.0.0" || host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            throw std::invalid_argument("Invalid IP address: " + host);
        }
    }

    return addr;
}

// ============================================================================
// TcpServer 实现
// ============================================================================

TcpServer::TcpServer(EventLoop* loop) : loop_(loop) {}

TcpServer::~TcpServer() {
    stop();
}

Task<void> TcpServer::listen(const std::string& host, uint16_t port) {
    listen_socket_ = std::make_unique<Socket>(loop_);

    if (!listen_socket_->bind(host, port)) {
        throw std::runtime_error("Failed to bind to " + host + ":" + std::to_string(port));
    }

    if (!listen_socket_->listen()) {
        throw std::runtime_error("Failed to listen on " + host + ":" + std::to_string(port));
    }

    running_.store(true, std::memory_order_release);

    // 启动接受连接的协程并持有其生命周期
    // 注意：Task<void> 使用 suspend_never，accept_loop() 调用时已同步运行
    // 直到第一个挂起点（co_await accept()）。无需再 schedule_resume，
    // 否则会提前唤醒仍在等待 epoll 事件的协程，导致 await_resume 拿到空 socket。
    accept_task_ = accept_loop();

    co_return;
}

void TcpServer::stop() {
    running_.store(false, std::memory_order_release);
    if (listen_socket_) {
        listen_socket_->close();
    }
    // 释放连接处理任务
    {
        std::lock_guard<std::mutex> lk(active_tasks_mutex_);
        active_tasks_.clear();
    }
}

Task<void> TcpServer::accept_loop() {
    while (running_.load(std::memory_order_acquire)) {
        try {
            auto client_socket = co_await listen_socket_->accept();

            if (connection_handler_) {
                // 创建连接处理任务并保存，防止任务在临时对象析构时被销毁
                // 注意：suspend_never 语义下，connection_handler_() 调用时协程体已
                // 同步运行到第一个真正的挂起点（等待 epoll 事件）。
                // 无需再 schedule_resume——多余的 resume 会在 await_resume() 中
                // 拿到还未就绪的值，导致 socket 为 null 的崩溃。
                auto task = connection_handler_(std::move(client_socket));
                auto task_ptr = std::make_shared<Task<void>>(std::move(task));
                
                {
                    std::lock_guard<std::mutex> lk(active_tasks_mutex_);
                    active_tasks_.push_back(task_ptr);
                }

                // 轻量清理：移除已完成/已取消的任务，避免容器无限增长
                {
                    std::lock_guard<std::mutex> lk(active_tasks_mutex_);
                    auto it = active_tasks_.begin();
                    while (it != active_tasks_.end()) {
                        const auto& t = *it;
                        if (!t || t->is_settled()) {
                            it = active_tasks_.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            if (running_.load(std::memory_order_acquire)) {
                // 记录错误但继续运行
                LOG_ERROR("EventLoop run_loop caught exception: %s", e.what());
            }
        }
    }

    co_return;
}

// ============================================================================
// TcpConnection 实现
// ============================================================================

TcpConnection::TcpConnection(std::unique_ptr<Socket> socket)
    : socket_(std::move(socket)) {}

TcpConnection::~TcpConnection() {
    close();
}

Task<std::string> TcpConnection::read_line() {
    // 先从缓冲区查找
    auto pos = read_buffer_.find('\n');
    if (pos != std::string::npos) {
        std::string line = read_buffer_.substr(0, pos + 1);
        read_buffer_.erase(0, pos + 1);
        co_return line;
    }

    // 需要读取更多数据
    while (true) {
        char buffer[4096];
        ssize_t n = co_await socket_->read(buffer, sizeof(buffer));

        if (n == 0) {
            // EOF，返回剩余数据
            std::string line = std::move(read_buffer_);
            read_buffer_.clear();
            co_return line;
        }

        if (n < 0) {
            throw std::runtime_error("Read failed");
        }

        read_buffer_.append(buffer, n);

        // 查找换行符
        pos = read_buffer_.find('\n');
        if (pos != std::string::npos) {
            std::string line = read_buffer_.substr(0, pos + 1);
            read_buffer_.erase(0, pos + 1);
            co_return line;
        }
    }
}

Task<std::string> TcpConnection::read(size_t size) {
    if (read_buffer_.size() >= size) {
        std::string data = read_buffer_.substr(0, size);
        read_buffer_.erase(0, size);
        co_return data;
    }

    std::string data = std::move(read_buffer_);
    read_buffer_.clear();

    while (data.size() < size) {
        char buffer[4096];
        size_t to_read = std::min(sizeof(buffer), size - data.size());

        ssize_t n = co_await socket_->read(buffer, to_read);
        if (n == 0) {
            break; // EOF
        }
        if (n < 0) {
            throw std::runtime_error("Read failed");
        }

        data.append(buffer, n);
    }

    co_return data;
}

Task<void> TcpConnection::write(const std::string& data) {
    write_buffer_ += data;
    co_return;
}

Task<void> TcpConnection::flush() {
    if (!write_buffer_.empty()) {
        co_await socket_->write_string(write_buffer_);
        write_buffer_.clear();
    }
    co_return;
}

void TcpConnection::close() {
    if (!closed_) {
        closed_ = true;
        if (socket_) {
            socket_->close();
        }
    }
}

} // namespace flowcoro::net
