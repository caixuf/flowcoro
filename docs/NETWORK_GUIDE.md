# FlowCoro 网络编程指南

## 概述

FlowCoro 提供了基于协程的异步网络编程接口，使网络IO操作变得简单而高效。本指南将介绍如何使用FlowCoro进行网络编程，从基础的Socket操作到复杂的服务器架构。

## 基础网络编程

### 1. Socket基础操作

#### 创建和连接Socket

```cpp
#include <flowcoro.hpp>

Task<void> client_example() {
    try {
        // 创建Socket
        auto socket = std::make_unique<flowcoro::net::Socket>();
        
        // 异步连接
        co_await socket->connect("www.example.com", 80);
        
        // 发送HTTP请求
        std::string request = "GET / HTTP/1.1\r\nHost: www.example.com\r\n\r\n";
        co_await socket->write_all(request.c_str(), request.size());
        
        // 接收响应
        char buffer[4096];
        size_t bytes_read = co_await socket->read(buffer, sizeof(buffer));
        
        std::cout << "收到响应: " << std::string(buffer, bytes_read) << std::endl;
        
    } catch (const flowcoro::NetworkException& e) {
        std::cerr << "网络错误: " << e.what() << std::endl;
    }
}
```

#### 服务端Socket操作

```cpp
Task<void> server_example() {
    auto server_socket = std::make_unique<flowcoro::net::Socket>();
    
    // 绑定地址
    co_await server_socket->bind("0.0.0.0", 8080);
    
    // 开始监听
    co_await server_socket->listen(128);
    
    std::cout << "服务器监听在 0.0.0.0:8080" << std::endl;
    
    while (true) {
        // 接受新连接
        auto client_socket = co_await server_socket->accept();
        
        // 在新协程中处理客户端
        auto handler = handle_client(std::move(client_socket));
        // 不等待处理完成，继续接受新连接
    }
}

Task<void> handle_client(std::unique_ptr<flowcoro::net::Socket> client) {
    try {
        char buffer[1024];
        while (true) {
            size_t bytes_read = co_await client->read(buffer, sizeof(buffer));
            if (bytes_read == 0) break;  // 连接关闭
            
            // Echo back to client
            co_await client->write_all(buffer, bytes_read);
        }
    } catch (const std::exception& e) {
        std::cerr << "客户端处理错误: " << e.what() << std::endl;
    }
}
```

### 2. TcpConnection高级接口

TcpConnection提供了更高级的接口，简化了常见的网络操作。

```cpp
Task<void> tcp_connection_example() {
    auto socket = std::make_unique<flowcoro::net::Socket>();
    co_await socket->connect("api.example.com", 80);
    
    auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
    
    // 发送行数据
    co_await conn->write("GET /api/data HTTP/1.1");
    co_await conn->write("Host: api.example.com");
    co_await conn->write("");  // 空行结束请求
    co_await conn->flush();
    
    // 逐行读取响应
    while (true) {
        auto line = co_await conn->read_line();
        if (line.empty()) break;
        
        std::cout << "收到: " << line << std::endl;
    }
}
```

### 3. 事件循环和多路复用

```cpp
Task<void> event_loop_example() {
    auto& loop = flowcoro::net::GlobalEventLoop::get();
    
    // 添加定时器
    loop.add_timer(std::chrono::seconds(5), []() {
        std::cout << "定时器触发" << std::endl;
    });
    
    // 启动事件循环
    auto loop_task = loop.run();
    
    // 在另一个协程中做其他工作
    auto work_task = do_background_work();
    
    // 等待事件循环完成
    co_await loop_task;
}
```

## TCP服务器开发

### 1. 基础TCP服务器

```cpp
class EchoServer {
private:
    flowcoro::net::TcpServer server_;
    std::atomic<bool> running_{false};
    
public:
    EchoServer() : server_(&flowcoro::net::GlobalEventLoop::get()) {
        // 设置连接处理器
        server_.set_connection_handler(
            [this](auto socket) { return handle_connection(std::move(socket)); });
    }
    
    Task<void> start(const std::string& host, uint16_t port) {
        running_ = true;
        
        std::cout << "启动Echo服务器在 " << host << ":" << port << std::endl;
        
        // 开始监听
        co_await server_.listen(host, port);
    }
    
    void stop() {
        running_ = false;
        server_.stop();
    }
    
private:
    Task<void> handle_connection(std::unique_ptr<flowcoro::net::Socket> socket) {
        auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
        
        std::cout << "新连接来自: " << conn->get_remote_address() 
                  << ":" << conn->get_remote_port() << std::endl;
        
        try {
            while (running_ && conn->is_connected()) {
                auto line = co_await conn->read_line();
                if (line.empty()) break;
                
                // Echo back with prefix
                co_await conn->write("Echo: " + line);
                co_await conn->flush();
            }
        } catch (const std::exception& e) {
            std::cerr << "连接处理错误: " << e.what() << std::endl;
        }
        
        std::cout << "连接关闭" << std::endl;
    }
};
```

### 2. HTTP服务器示例

```cpp
class SimpleHttpServer {
private:
    flowcoro::net::TcpServer server_;
    std::unordered_map<std::string, std::function<Task<std::string>(const HttpRequest&)>> routes_;
    
public:
    SimpleHttpServer() : server_(&flowcoro::net::GlobalEventLoop::get()) {
        server_.set_connection_handler(
            [this](auto socket) { return handle_http_connection(std::move(socket)); });
    }
    
    // 注册路由处理器
    void route(const std::string& path, 
               std::function<Task<std::string>(const HttpRequest&)> handler) {
        routes_[path] = std::move(handler);
    }
    
    Task<void> listen(const std::string& host, uint16_t port) {
        co_await server_.listen(host, port);
    }
    
private:
    Task<void> handle_http_connection(std::unique_ptr<flowcoro::net::Socket> socket) {
        auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
        
        try {
            while (conn->is_connected()) {
                // 解析HTTP请求
                auto request = co_await parse_http_request(*conn);
                if (!request) break;
                
                // 查找路由处理器
                auto it = routes_.find(request->path);
                if (it != routes_.end()) {
                    auto response_body = co_await it->second(*request);
                    co_await send_http_response(*conn, 200, response_body);
                } else {
                    co_await send_http_response(*conn, 404, "Not Found");
                }
                
                // HTTP/1.1 keep-alive
                if (request->headers.find("Connection")->second == "close") {
                    break;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "HTTP请求处理错误: " << e.what() << std::endl;
        }
    }
    
    Task<std::optional<HttpRequest>> parse_http_request(flowcoro::net::TcpConnection& conn) {
        HttpRequest request;
        
        // 读取请求行
        auto request_line = co_await conn.read_line();
        if (request_line.empty()) co_return std::nullopt;
        
        std::istringstream iss(request_line);
        iss >> request.method >> request.path >> request.version;
        
        // 读取请求头
        while (true) {
            auto header_line = co_await conn.read_line();
            if (header_line.empty()) break;
            
            auto colon_pos = header_line.find(':');
            if (colon_pos != std::string::npos) {
                auto key = header_line.substr(0, colon_pos);
                auto value = header_line.substr(colon_pos + 2);  // 跳过": "
                request.headers[key] = value;
            }
        }
        
        co_return request;
    }
    
    Task<void> send_http_response(flowcoro::net::TcpConnection& conn, 
                                  int status_code, const std::string& body) {
        co_await conn.write("HTTP/1.1 " + std::to_string(status_code) + " OK");
        co_await conn.write("Content-Type: text/plain");
        co_await conn.write("Content-Length: " + std::to_string(body.size()));
        co_await conn.write("");  // 空行
        co_await conn.write(body);
        co_await conn.flush();
    }
};

// 使用示例
Task<void> run_http_server() {
    SimpleHttpServer server;
    
    // 注册路由
    server.route("/", [](const HttpRequest& req) -> Task<std::string> {
        co_return "Hello, World!";
    });
    
    server.route("/api/time", [](const HttpRequest& req) -> Task<std::string> {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        co_return std::ctime(&time_t);
    });
    
    std::cout << "HTTP服务器启动在 http://localhost:8080" << std::endl;
    co_await server.listen("0.0.0.0", 8080);
}
```

### 3. 负载均衡和连接池

```cpp
class LoadBalancedClient {
private:
    std::vector<std::string> backend_hosts_;
    std::vector<uint16_t> backend_ports_;
    std::atomic<size_t> current_backend_{0};
    
    // 每个后端的连接池
    std::vector<std::unique_ptr<ConnectionPool>> connection_pools_;
    
public:
    LoadBalancedClient(const std::vector<std::pair<std::string, uint16_t>>& backends) {
        for (const auto& backend : backends) {
            backend_hosts_.push_back(backend.first);
            backend_ports_.push_back(backend.second);
            connection_pools_.emplace_back(std::make_unique<ConnectionPool>(10));  // 每个后端10个连接
        }
    }
    
    Task<std::string> send_request(const std::string& request) {
        // 轮询选择后端
        size_t backend_index = current_backend_++ % backend_hosts_.size();
        
        // 从连接池获取连接
        auto conn = co_await connection_pools_[backend_index]->acquire_connection(
            backend_hosts_[backend_index], backend_ports_[backend_index]);
        
        try {
            // 发送请求
            co_await conn->write(request);
            co_await conn->flush();
            
            // 接收响应
            auto response = co_await conn->read_line();
            
            // 归还连接到池中
            connection_pools_[backend_index]->release_connection(std::move(conn));
            
            co_return response;
            
        } catch (const std::exception& e) {
            // 连接出错，不归还到池中
            std::cerr << "请求失败: " << e.what() << std::endl;
            throw;
        }
    }
};
```

## 高级网络模式

### 1. 流水线处理

```cpp
class PipelineProcessor {
private:
    flowcoro::LockfreeQueue<std::unique_ptr<NetworkTask>> task_queue_;
    std::vector<std::unique_ptr<WorkerCoroutine>> workers_;
    
public:
    PipelineProcessor(size_t num_workers) : task_queue_(1024) {
        for (size_t i = 0; i < num_workers; ++i) {
            workers_.emplace_back(std::make_unique<WorkerCoroutine>(i, &task_queue_));
        }
    }
    
    Task<void> process_connection(std::unique_ptr<flowcoro::net::Socket> socket) {
        auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
        
        while (conn->is_connected()) {
            try {
                // 读取请求
                auto request_data = co_await conn->read_bytes(1024);
                if (request_data.empty()) break;
                
                // 创建处理任务
                auto task = std::make_unique<NetworkTask>(std::move(conn), request_data);
                
                // 提交到处理队列
                while (!task_queue_.enqueue(std::move(task))) {
                    // 队列满，等待一段时间
                    co_await flowcoro::sleep_for(std::chrono::milliseconds(1));
                }
                
                // 获取新的连接对象继续处理下一个请求
                conn = co_await task->get_connection_back();
                
            } catch (const std::exception& e) {
                std::cerr << "流水线处理错误: " << e.what() << std::endl;
                break;
            }
        }
    }
};
```

### 2. 发布-订阅模式

```cpp
class PubSubServer {
private:
    std::unordered_map<std::string, std::vector<std::weak_ptr<Subscriber>>> channels_;
    std::mutex channels_mutex_;
    flowcoro::net::TcpServer server_;
    
public:
    PubSubServer() : server_(&flowcoro::net::GlobalEventLoop::get()) {
        server_.set_connection_handler(
            [this](auto socket) { return handle_pubsub_connection(std::move(socket)); });
    }
    
    Task<void> listen(const std::string& host, uint16_t port) {
        co_await server_.listen(host, port);
    }
    
    void publish(const std::string& channel, const std::string& message) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        
        auto it = channels_.find(channel);
        if (it != channels_.end()) {
            // 清理失效的订阅者
            auto& subscribers = it->second;
            subscribers.erase(
                std::remove_if(subscribers.begin(), subscribers.end(),
                    [](const std::weak_ptr<Subscriber>& weak_sub) {
                        return weak_sub.expired();
                    }),
                subscribers.end());
            
            // 发送消息给所有订阅者
            for (const auto& weak_sub : subscribers) {
                if (auto sub = weak_sub.lock()) {
                    sub->send_message(message);
                }
            }
        }
    }
    
private:
    Task<void> handle_pubsub_connection(std::unique_ptr<flowcoro::net::Socket> socket) {
        auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
        auto subscriber = std::make_shared<Subscriber>(std::move(conn));
        
        try {
            while (subscriber->is_connected()) {
                auto command = co_await subscriber->read_command();
                
                if (command.type == "SUBSCRIBE") {
                    subscribe_to_channel(command.channel, subscriber);
                } else if (command.type == "UNSUBSCRIBE") {
                    unsubscribe_from_channel(command.channel, subscriber);
                } else if (command.type == "PUBLISH") {
                    publish(command.channel, command.message);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "PubSub连接错误: " << e.what() << std::endl;
        }
    }
    
    void subscribe_to_channel(const std::string& channel, 
                            std::shared_ptr<Subscriber> subscriber) {
        std::lock_guard<std::mutex> lock(channels_mutex_);
        channels_[channel].emplace_back(subscriber);
    }
};
```

### 3. 反向代理

```cpp
class ReverseProxy {
private:
    flowcoro::net::TcpServer frontend_server_;
    std::vector<std::pair<std::string, uint16_t>> backend_servers_;
    std::atomic<size_t> current_backend_{0};
    
public:
    ReverseProxy(const std::vector<std::pair<std::string, uint16_t>>& backends)
        : frontend_server_(&flowcoro::net::GlobalEventLoop::get())
        , backend_servers_(backends) {
        
        frontend_server_.set_connection_handler(
            [this](auto socket) { return handle_proxy_connection(std::move(socket)); });
    }
    
    Task<void> listen(const std::string& host, uint16_t port) {
        std::cout << "反向代理监听在 " << host << ":" << port << std::endl;
        co_await frontend_server_.listen(host, port);
    }
    
private:
    Task<void> handle_proxy_connection(std::unique_ptr<flowcoro::net::Socket> client_socket) {
        auto client_conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(client_socket));
        
        // 选择后端服务器
        size_t backend_index = current_backend_++ % backend_servers_.size();
        const auto& backend = backend_servers_[backend_index];
        
        try {
            // 连接到后端服务器
            auto backend_socket = std::make_unique<flowcoro::net::Socket>();
            co_await backend_socket->connect(backend.first, backend.second);
            auto backend_conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(backend_socket));
            
            // 启动双向数据转发
            auto forward_task = forward_data(*client_conn, *backend_conn);
            auto backward_task = forward_data(*backend_conn, *client_conn);
            
            // 等待任一方向的连接断开
            // TODO: 实现 when_any 等待任一协程完成
            co_await forward_task;
            
        } catch (const std::exception& e) {
            std::cerr << "代理连接错误: " << e.what() << std::endl;
        }
    }
    
    Task<void> forward_data(flowcoro::net::TcpConnection& from, 
                           flowcoro::net::TcpConnection& to) {
        try {
            char buffer[8192];
            while (from.is_connected() && to.is_connected()) {
                auto bytes_read = co_await from.read_bytes(sizeof(buffer));
                if (bytes_read.empty()) break;
                
                co_await to.write_bytes(bytes_read);
                co_await to.flush();
            }
        } catch (const std::exception& e) {
            // 连接断开是正常情况
        }
    }
};
```

## 错误处理和重连

### 1. 自动重连机制

```cpp
class ReliableClient {
private:
    std::string host_;
    uint16_t port_;
    std::unique_ptr<flowcoro::net::TcpConnection> conn_;
    std::atomic<bool> connected_{false};
    
public:
    ReliableClient(const std::string& host, uint16_t port) 
        : host_(host), port_(port) {}
    
    Task<void> connect_with_retry(int max_retries = 5) {
        for (int attempt = 0; attempt < max_retries; ++attempt) {
            try {
                auto socket = std::make_unique<flowcoro::net::Socket>();
                co_await socket->connect(host_, port_);
                conn_ = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
                connected_ = true;
                
                std::cout << "连接成功到 " << host_ << ":" << port_ << std::endl;
                co_return;
                
            } catch (const std::exception& e) {
                std::cerr << "连接失败 (尝试 " << (attempt + 1) << "/" << max_retries 
                          << "): " << e.what() << std::endl;
                
                if (attempt < max_retries - 1) {
                    // 指数退避
                    auto delay = std::chrono::milliseconds(100 * (1 << attempt));
                    co_await flowcoro::sleep_for(delay);
                }
            }
        }
        
        throw std::runtime_error("无法连接到服务器");
    }
    
    Task<std::string> send_with_retry(const std::string& request) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            try {
                if (!connected_ || !conn_->is_connected()) {
                    co_await connect_with_retry();
                }
                
                co_await conn_->write(request);
                co_await conn_->flush();
                
                auto response = co_await conn_->read_line();
                co_return response;
                
            } catch (const std::exception& e) {
                std::cerr << "发送失败: " << e.what() << std::endl;
                connected_ = false;
                conn_.reset();
                
                if (attempt < 2) {
                    co_await flowcoro::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        
        throw std::runtime_error("发送请求失败");
    }
};
```

### 2. 超时处理

```cpp
template<typename T>
Task<T> with_timeout(Task<T> task, std::chrono::milliseconds timeout) {
    // TODO: 实现协程超时机制
    // 这里需要实现一个通用的超时包装器
    co_return co_await task;
}

Task<void> timeout_example() {
    try {
        auto socket = std::make_unique<flowcoro::net::Socket>();
        
        // 连接超时5秒
        co_await with_timeout(
            socket->connect("slow-server.example.com", 80),
            std::chrono::seconds(5)
        );
        
        // 读取超时10秒
        char buffer[1024];
        auto bytes_read = co_await with_timeout(
            socket->read(buffer, sizeof(buffer)),
            std::chrono::seconds(10)
        );
        
        std::cout << "读取了 " << bytes_read << " 字节" << std::endl;
        
    } catch (const TimeoutException& e) {
        std::cerr << "操作超时: " << e.what() << std::endl;
    }
}
```

## 性能优化技巧

### 1. 批量操作

```cpp
class BatchingTcpConnection {
private:
    std::unique_ptr<flowcoro::net::TcpConnection> conn_;
    std::vector<std::string> write_buffer_;
    
public:
    BatchingTcpConnection(std::unique_ptr<flowcoro::net::Socket> socket)
        : conn_(std::make_unique<flowcoro::net::TcpConnection>(std::move(socket))) {}
    
    void queue_write(const std::string& data) {
        write_buffer_.push_back(data);
    }
    
    Task<void> flush_batch() {
        if (write_buffer_.empty()) co_return;
        
        // 合并所有待发送数据
        std::string combined;
        for (const auto& data : write_buffer_) {
            combined += data + "\n";
        }
        
        co_await conn_->write(combined);
        co_await conn_->flush();
        
        write_buffer_.clear();
    }
    
    Task<std::vector<std::string>> read_batch(size_t max_lines) {
        std::vector<std::string> lines;
        lines.reserve(max_lines);
        
        for (size_t i = 0; i < max_lines; ++i) {
            try {
                auto line = co_await conn_->read_line();
                if (line.empty()) break;
                lines.push_back(line);
            } catch (const std::exception&) {
                break;
            }
        }
        
        co_return lines;
    }
};
```

### 2. 内存池和缓冲区复用

```cpp
class NetworkBufferManager {
private:
    flowcoro::ObjectPool<std::vector<uint8_t>> buffer_pool_;
    static constexpr size_t BUFFER_SIZE = 64 * 1024;  // 64KB
    
public:
    NetworkBufferManager() : buffer_pool_(50) {  // 预分配50个缓冲区
        // 预填充缓冲区池
        for (size_t i = 0; i < 50; ++i) {
            auto buffer = std::make_unique<std::vector<uint8_t>>();
            buffer->resize(BUFFER_SIZE);
            buffer_pool_.release(std::move(buffer));
        }
    }
    
    auto acquire_buffer() {
        return buffer_pool_.acquire();
    }
    
    void release_buffer(std::unique_ptr<std::vector<uint8_t>> buffer) {
        buffer->clear();
        buffer->resize(BUFFER_SIZE);
        buffer_pool_.release(std::move(buffer));
    }
};

// 全局缓冲区管理器
thread_local NetworkBufferManager g_buffer_manager;
```

## 测试和调试

### 1. 网络单元测试

```cpp
class NetworkTestServer {
private:
    flowcoro::net::TcpServer server_;
    std::function<Task<void>(flowcoro::net::TcpConnection&)> handler_;
    
public:
    NetworkTestServer() : server_(&flowcoro::net::GlobalEventLoop::get()) {
        server_.set_connection_handler([this](auto socket) {
            return handle_test_connection(std::move(socket));
        });
    }
    
    void set_handler(std::function<Task<void>(flowcoro::net::TcpConnection&)> handler) {
        handler_ = std::move(handler);
    }
    
    Task<void> listen_on_random_port() {
        co_await server_.listen("127.0.0.1", 0);  // 随机端口
    }
    
    uint16_t get_port() const {
        return server_.get_port();
    }
    
private:
    Task<void> handle_test_connection(std::unique_ptr<flowcoro::net::Socket> socket) {
        auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
        
        if (handler_) {
            co_await handler_(*conn);
        }
    }
};

// 测试用例
Task<void> test_echo_server() {
    NetworkTestServer test_server;
    
    // 设置echo处理器
    test_server.set_handler([](flowcoro::net::TcpConnection& conn) -> Task<void> {
        auto line = co_await conn.read_line();
        co_await conn.write("Echo: " + line);
        co_await conn.flush();
    });
    
    co_await test_server.listen_on_random_port();
    uint16_t port = test_server.get_port();
    
    // 创建客户端测试
    auto client_socket = std::make_unique<flowcoro::net::Socket>();
    co_await client_socket->connect("127.0.0.1", port);
    auto client_conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(client_socket));
    
    // 发送测试数据
    co_await client_conn->write("Hello, Test!");
    co_await client_conn->flush();
    
    // 验证响应
    auto response = co_await client_conn->read_line();
    assert(response == "Echo: Hello, Test!");
    
    std::cout << "Echo服务器测试通过" << std::endl;
}
```

### 2. 性能基准测试

```cpp
Task<void> benchmark_network_throughput() {
    constexpr size_t NUM_CONNECTIONS = 100;
    constexpr size_t MESSAGES_PER_CONNECTION = 1000;
    
    // 启动测试服务器
    NetworkTestServer server;
    server.set_handler([](flowcoro::net::TcpConnection& conn) -> Task<void> {
        while (conn.is_connected()) {
            auto data = co_await conn.read_bytes(1024);
            if (data.empty()) break;
            co_await conn.write_bytes(data);  // Echo back
            co_await conn.flush();
        }
    });
    
    co_await server.listen_on_random_port();
    uint16_t port = server.get_port();
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 创建多个并发客户端
    std::vector<Task<void>> client_tasks;
    for (size_t i = 0; i < NUM_CONNECTIONS; ++i) {
        client_tasks.emplace_back(benchmark_client("127.0.0.1", port, MESSAGES_PER_CONNECTION));
    }
    
    // 等待所有客户端完成
    for (auto& task : client_tasks) {
        co_await task;
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    size_t total_messages = NUM_CONNECTIONS * MESSAGES_PER_CONNECTION;
    double throughput = static_cast<double>(total_messages) / duration.count() * 1000;
    
    std::cout << "网络吞吐量: " << throughput << " 消息/秒" << std::endl;
}

Task<void> benchmark_client(const std::string& host, uint16_t port, size_t num_messages) {
    auto socket = std::make_unique<flowcoro::net::Socket>();
    co_await socket->connect(host, port);
    auto conn = std::make_unique<flowcoro::net::TcpConnection>(std::move(socket));
    
    std::string test_message = "This is a test message for benchmarking.";
    
    for (size_t i = 0; i < num_messages; ++i) {
        co_await conn->write(test_message);
        co_await conn->flush();
        
        auto response = co_await conn->read_line();
        // 验证响应正确性
        assert(response == test_message);
    }
}
```

通过这个网络编程指南，你可以掌握使用FlowCoro进行高效网络编程的各种技巧和最佳实践。从简单的客户端-服务器通信到复杂的高性能网络服务架构，FlowCoro都能提供强大的支持。
