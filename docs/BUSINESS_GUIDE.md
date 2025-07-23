# FlowCoro 业务程序员快速上手指南

## 🎯 5分钟理解协程的价值

### 问题：传统方式的痛点
```cpp
// 😫 传统同步代码：慢！
void process_order() {
    auto user = fetch_user(123);        // 等待50ms
    auto product = fetch_product(456);  // 等待30ms  
    auto stock = check_stock(456);      // 等待20ms
    // 总计：100ms，串行等待
}
```

### 解决方案：协程并发
```cpp
// 🚀 协程异步代码：快！
Task<void> process_order() {
    auto user_task = fetch_user(123);     // 立即返回
    auto product_task = fetch_product(456); // 立即返回
    auto stock_task = check_stock(456);   // 立即返回
    
    auto user = co_await user_task;       // 三个任务并行执行
    auto product = co_await product_task; // 只需等待最慢的那个
    auto stock = co_await stock_task;     // 总计：~50ms
}
```

**性能提升：2-3倍！**

## 🏪 实际业务场景

### 1. 电商订单处理
```bash
cd examples
./business_demo
```

展示内容：
- ✅ 并发查询用户、商品、库存
- ✅ 批量处理多个订单
- ✅ 同步vs异步性能对比
- ✅ 真实业务逻辑流程

### 2. Web API服务
```cpp
// 一个Web接口可以同时处理多个请求
Task<json> handle_user_profile(int user_id) {
    auto user_task = db.get_user(user_id);
    auto posts_task = db.get_user_posts(user_id);
    auto friends_task = db.get_user_friends(user_id);
    
    // 三个数据库查询并发执行
    auto user = co_await user_task;
    auto posts = co_await posts_task;
    auto friends = co_await friends_task;
    
    co_return build_profile_json(user, posts, friends);
}
```

### 3. 微服务调用
```cpp
Task<OrderResponse> create_order(OrderRequest req) {
    // 同时调用多个微服务
    auto user_service = call_user_service(req.user_id);
    auto inventory_service = call_inventory_service(req.items);
    auto payment_service = call_payment_service(req.payment);
    
    // 等待所有服务响应
    auto user_info = co_await user_service;
    auto inventory_check = co_await inventory_service;
    auto payment_result = co_await payment_service;
    
    co_return create_order_response(user_info, inventory_check, payment_result);
}
```

## 📚 学习路径：从简单到复杂

### 第1天：理解基本概念
1. 什么是协程？→ 可以暂停和恢复的函数
2. 为什么要用？→ 并发处理，提升性能
3. 怎么写？→ `co_await`、`co_return`

### 第2天：写第一个协程
```cpp
Task<std::string> my_first_coroutine() {
    co_await sleep_for(100ms);  // 暂停100ms
    co_return "Hello Coroutine!";  // 返回结果
}
```

### 第3天：并发处理多个任务
```cpp
Task<void> parallel_tasks() {
    auto task1 = fetch_data("url1");
    auto task2 = fetch_data("url2");
    
    auto result1 = co_await task1;  // 并发执行
    auto result2 = co_await task2;  // 而不是串行
}
```

### 第4天：实际业务应用
- 参考 `business_demo.cpp`
- 电商、社交、金融等场景

### 第5天：性能优化和最佳实践
- 错误处理
- 资源管理
- 性能调优

## 🤔 常见问题 FAQ

### Q1: 协程比线程好在哪？
**A:** 
- 线程：重量级，切换开销大，难以管理
- 协程：轻量级，切换开销小，代码简洁

### Q2: 什么时候适合用协程？
**A:**
- ✅ 有大量IO操作（数据库、网络、文件）
- ✅ 需要并发处理多个任务
- ✅ 响应时间要求高的应用
- ❌ CPU密集型计算（用线程池更好）

### Q3: 学习成本高吗？
**A:**
- 基础使用：1-2天上手
- 熟练掌握：1-2周
- 比学习多线程简单得多！

### Q4: 生产环境稳定吗？
**A:**
- C++20协程是标准特性
- FlowCoro经过充分测试
- 已有实际项目应用案例

## 🎯 立即开始

1. **克隆项目**：
   ```bash
   git clone <repo-url>
   cd flowcord
   ```

2. **编译运行**：
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ./examples/business_demo
   ```

3. **修改示例代码**，体验协程的威力！

## 💡 关键心得

> **协程 = 让异步代码看起来像同步代码**
> 
> **性能 = 串行 → 并行的巨大提升**
> 
> **代码 = 简洁清晰，易于维护**

开始你的协程之旅吧！🚀
