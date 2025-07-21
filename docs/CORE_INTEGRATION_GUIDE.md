# 🚀 FlowCoro 核心整合指南

## 📝 概述

FlowCoro 2.1版本已完成了重要的核心整合工作，将原本分散的生命周期管理功能（lifecycle）完全整合到了核心库中。本文档提供了整合过程的详细信息、架构变化以及使用指南。

## 🔄 整合内容

### 整合到核心的功能

以下功能已经从独立的lifecycle模块整合到core.h中：

1. **协程状态管理**
   - `coroutine_state` 枚举
   - `coroutine_state_manager` 类

2. **取消机制**
   - `cancellation_state` 类
   - `cancellation_token` 接口
   - `cancellation_source` 接口

3. **安全协程句柄**
   - `safe_coroutine_handle` 增强包装

### 移除的冗余模块

整合过程中，以下冗余模块已被移除：

- `lifecycle.h` - 功能已整合到core.h
- `lifecycle_v2.h` - 功能已整合到core.h
- `lifecycle/` 目录 - 全部功能已整合

## 📋 架构变化

### 之前的架构

```
include/flowcoro/
├── core.h              # 基础协程功能
└── lifecycle/          # 独立的生命周期管理
    ├── core.h
    ├── cancellation.h
    └── enhanced_task.h
```

### 现在的架构

```
include/flowcoro/
├── core.h                          # 整合了所有生命周期管理
├── core_simple.h                   # 简化独立版本
├── flowcoro_cancellation.h         # 取消机制
└── flowcoro_enhanced_task.h        # 增强Task实现
```

## 💡 使用指南

### 基础使用

```cpp
#include "flowcoro/core.h"

using namespace flowcoro;

Task<int> my_task() {
    // 协程现在自动支持生命周期管理
    co_return 42;
}

void example() {
    auto task = my_task();
    
    // 使用生命周期管理功能
    if (!task.is_cancelled()) {
        auto state = task.get_state();
        auto lifetime = task.get_lifetime();
        
        int result = task.get(); // 如常使用
    }
}
```

### 取消支持

```cpp
// 创建可取消的任务
Task<std::string> long_running(cancellation_token token) {
    for (int i = 0; i < 100; i++) {
        // 检查取消状态
        if (token.is_cancelled()) {
            co_return "Cancelled";
        }
        co_await sleep_for(std::chrono::milliseconds(10));
    }
    co_return "Complete";
}

// 使用取消功能
void cancel_example() {
    cancellation_source source;
    auto task = long_running(source.get_token());
    
    // 在其他线程或异步操作中
    source.request_cancellation();
}
```

### 超时支持

```cpp
// 使用超时功能
Task<int> with_timeout_example() {
    auto timeout_token = cancellation_token::create_timeout(
        std::chrono::milliseconds(500)
    );
    
    try {
        auto result = co_await long_running(timeout_token);
        co_return 1;
    }
    catch (operation_cancelled_exception&) {
        co_return 0; // 超时
    }
}
```

## 🔍 技术说明

### 整合优势

1. **简化依赖**：不再需要单独包含lifecycle相关头文件
2. **统一API**：所有核心功能使用一致的API
3. **优化性能**：减少了间接调用，改进了内部实现
4. **减少代码重复**：消除了多个模块中的重复代码
5. **向后兼容**：现有代码无需修改，自动获得增强功能

### 性能影响

整合后的性能变化：
- 内存占用：每个Task增加约16字节（状态管理）
- CPU开销：几乎为零，大多数功能为内联实现
- 编译时间：稍有增加，但使用预编译头可以缓解

## 🚀 迁移指南

如果您正在使用旧版本的lifecycle功能，请按照以下步骤迁移：

1. 将包含语句从`#include "flowcoro/lifecycle.h"`更改为`#include "flowcoro/core.h"`
2. 移除对lifecycle子目录下头文件的直接引用
3. 继续使用相同的API，所有功能仍然可用

## 📚 相关文档

- [API参考手册](API_REFERENCE.md) - 核心API详细说明
- [性能调优指南](PERFORMANCE_GUIDE.md) - 性能优化建议
- [集成报告](../INTEGRATION_REPORT.md) - 全面集成状态报告
