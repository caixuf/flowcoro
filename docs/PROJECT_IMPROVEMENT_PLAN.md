# FlowCoro 项目改进计划

> **基于核心架构已完成，外围组件需适配的现状制定**

## 🎯 项目现状评估

### ✅ 已完成的核心能力
- **协程调度器**: 事件循环+工作窃取线程池，性能验证9倍提升
- **内存管理**: 内存池、对象池、RAII生命周期管理
- **并发基础**: 无锁队列、线程安全协程句柄、Promise机制
- **数据库层**: 连接池、异步查询、事务支持(100%测试通过)
- **文档体系**: 13个详细指南，覆盖API、性能、业务场景

### ❌ 亟需修复的缺陷

#### 🚨 **Critical (阻塞性问题)**
1. **网络层Mock化**: HttpRequest只有模拟实现，无真实网络功能
2. **RPC系统禁用**: 主头文件中被注释掉，无法使用
3. **测试稳定性**: 内存池测试失败+协程销毁段错误

#### ⚠️ **High (功能完整性)**
4. **文档API不符**: 大量引用不存在的网络API
5. **示例代码无效**: network_example.cpp等无法编译运行

#### 💡 **Medium (用户体验)**
6. **错误处理不足**: 网络异常、连接超时缺乏处理
7. **监控可观测性**: 缺乏性能指标、健康检查
8. **生产级特性**: 缺少连接池、熔断器、限流等

## 🚀 分阶段改进路线图

### **Phase 1: 紧急修复 (Q3 2025, 优先级: P0)**

#### **1.1 网络层实现 (4周)**

**目标**: 从Mock实现转向真实网络功能

**交付物**:
```cpp
// 真实的Socket实现
namespace flowcoro::net {
    class Socket {
        Task<void> connect(const std::string& host, uint16_t port);
        Task<size_t> read(void* buffer, size_t size);
        Task<size_t> write(const void* buffer, size_t size);
        Task<void> close();
    };
    
    class TcpServer {
        Task<void> listen(const std::string& host, uint16_t port);
        Task<std::unique_ptr<Socket>> accept();
    };
}
```

**技术方案**:
- Linux: epoll + non-blocking sockets
- 协程化IO操作，集成现有事件循环
- 连接池管理，支持keep-alive

**验收标准**:
- [ ] examples/network_example.cpp 可以编译运行
- [ ] 真实HTTP请求替换Mock实现
- [ ] 网络测试套件100%通过

#### **1.2 测试稳定性修复 (2周)**

**问题定位**:
1. 内存池测试: `ptr == nullptr`断言失败
2. Sleep测试: 协程销毁时段错误

**修复方案**:
```cpp
// 修复内存池边界条件
template<typename T>
class MemoryPool {
    // 增加内存池耗尽时的fallback机制
    T* allocate() {
        if (auto ptr = try_allocate_from_pool()) {
            return ptr;
        }
        // Fallback to heap allocation
        return new T();
    }
};

// 修复协程销毁竞争条件
class SafeCoroutineHandle {
    // 增加销毁状态检查
    void safe_resume() {
        if (is_valid() && !is_destroying()) {
            handle_.resume();
        }
    }
};
```

**验收标准**:
- [ ] 所有测试套件100%通过
- [ ] Valgrind检查无内存泄漏
- [ ] AddressSanitizer检查无段错误

#### **1.3 RPC系统激活 (2周)**

**目标**: 恢复RPC功能，集成网络层

```cpp
// 重新启用RPC
#include "flowcoro/rpc.h"  // 取消注释

// 集成真实网络
class AsyncRpcServer {
    Task<void> start_tcp_server(uint16_t port) {
        TcpServer server;
        co_await server.listen("0.0.0.0", port);
        
        while (true) {
            auto client = co_await server.accept();
            // 启动RPC处理协程
            schedule_rpc_handler(std::move(client));
        }
    }
};
```

**验收标准**:
- [ ] RPC/TCP集成示例可运行
- [ ] RPC测试套件100%通过
- [ ] 支持JSON-RPC over TCP

### **Phase 2: 功能完善 (Q4 2025, 优先级: P1)**

#### **2.1 HTTP协议栈 (6周)**

**目标**: 完整的HTTP/1.1实现

```cpp
// HTTP服务器实现
namespace flowcoro::http {
    class HttpServer {
        void route(const std::string& path, Handler handler);
        Task<void> listen(const std::string& host, uint16_t port);
    };
    
    class HttpClient {
        Task<HttpResponse> get(const std::string& url);
        Task<HttpResponse> post(const std::string& url, const std::string& body);
    };
}
```

**功能特性**:
- HTTP/1.1 keep-alive支持
- 请求路由、中间件机制
- JSON/Form数据解析
- WebSocket升级支持

#### **2.2 文档API一致性 (3周)**

**任务清单**:
- [ ] 审计所有文档中的API引用
- [ ] 删除或替换不存在的API示例
- [ ] 新增真实网络API的使用示例
- [ ] 更新API_REFERENCE.md以反映实际接口

#### **2.3 生产级特性 (4周)**

```cpp
// 连接池实现
template<typename ConnectionType>
class ConnectionPool {
    Task<std::shared_ptr<ConnectionType>> acquire();
    void release(std::shared_ptr<ConnectionType> conn);
    void set_max_connections(size_t max);
    void set_idle_timeout(std::chrono::seconds timeout);
};

// 熔断器实现
class CircuitBreaker {
    enum State { CLOSED, OPEN, HALF_OPEN };
    Task<bool> try_execute(std::function<Task<void>()> operation);
};
```

### **Phase 3: 企业级增强 (Q1 2026, 优先级: P2)**

#### **3.1 可观测性系统**

```cpp
// 性能监控
class MetricsCollector {
    void record_request_latency(std::chrono::milliseconds latency);
    void increment_counter(const std::string& name);
    Task<std::string> export_prometheus_metrics();
};

// 分布式追踪
class TraceContext {
    std::string trace_id();
    std::string span_id();
    void add_tag(const std::string& key, const std::string& value);
};
```

#### **3.2 高级网络模式**

- 负载均衡算法(轮询、加权、一致性哈希)
- SSL/TLS支持(基于OpenSSL)
- HTTP/2协议支持
- gRPC集成

#### **3.3 微服务生态**

- 服务注册发现(Consul/etcd集成)
- API网关功能
- 配置中心集成
- 消息队列支持(Redis/RabbitMQ)

## 📊 优先级矩阵

| 任务类别 | 业务影响 | 技术复杂度 | 工期 | 优先级 |
|---------|---------|-----------|------|--------|
| 网络层实现 | 🔴 Critical | 🟡 Medium | 4周 | **P0** |
| 测试稳定性 | 🔴 Critical | 🟢 Low | 2周 | **P0** |
| RPC系统激活 | 🟠 High | 🟢 Low | 2周 | **P0** |
| HTTP协议栈 | 🟠 High | 🔴 High | 6周 | **P1** |
| 文档一致性 | 🟡 Medium | 🟢 Low | 3周 | **P1** |
| 可观测性 | 🟡 Medium | 🟡 Medium | 4周 | **P2** |

## 🎯 成功标准

### **短期目标 (Phase 1完成)**
- [ ] **功能完整性**: 网络、RPC功能可用，文档示例可运行
- [ ] **测试稳定性**: 所有测试套件100%通过，无内存泄漏
- [ ] **用户体验**: 5分钟内可搭建HTTP服务器，示例代码就绪

### **中期目标 (Phase 2完成)**
- [ ] **生产就绪**: HTTP/1.1服务器可处理1万并发连接
- [ ] **开发者友好**: 完整API文档，丰富示例，错误处理完善
- [ ] **性能优势**: 协程vs线程性能优势保持在5倍以上

### **长期目标 (Phase 3完成)**
- [ ] **企业级**: 可观测性、熔断器、限流等生产特性完备
- [ ] **生态集成**: 与主流中间件(Redis/MySQL/etcd)无缝集成
- [ ] **社区认可**: GitHub 1000+ stars，技术博客推广

## 💡 风险评估与缓解

### **技术风险**
- **网络层复杂度**: 采用渐进式实现，先基础Socket后高级特性
- **性能回归**: 每个阶段都需要性能基准测试验证
- **兼容性问题**: 保持现有API不变，新功能采用命名空间隔离

### **工期风险**
- **资源投入**: Phase 1为关键路径，需要集中资源突破
- **依赖管理**: 网络层是后续功能基础，优先级最高
- **质量控制**: 每个Phase都需要完整的测试覆盖

## 🏆 项目价值定位

### **技术价值**
- **教育意义**: 展示C++20协程在高性能网络编程中的应用
- **工程实践**: 从核心算法到完整系统的工程化过程
- **性能标杆**: 协程vs传统多线程的对比参考

### **商业价值**
- **技术栈选型**: 为企业C++协程技术选型提供参考
- **人才培养**: 协程技术学习和实战平台
- **开源贡献**: 推动C++协程生态发展

---

**结论**: FlowCoro核心架构已经非常扎实，当前的主要任务是将外围的网络、RPC、文档等组件与核心能力对齐，形成一个功能完整、文档准确、测试稳定的协程库。按照这个计划，6个月内可以达到生产级别的质量标准。
