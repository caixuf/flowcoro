# FlowCoro 核心测试配置
cmake_minimum_required(VERSION 3.16)

# 确保使用正确的 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项（内存优化版本）
# 注意：sanitizer 构建需要 -g 与 -O1，因此仅在未启用 sanitizer 时强制 -g0 -O0。
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines -pthread -fno-stack-protector")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=128 -ftemplate-backtrace-limit=10")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --param ggc-min-expand=10 --param ggc-min-heapsize=32768")
if(NOT FLOWCORO_ENABLE_TSAN AND NOT FLOWCORO_ENABLE_SANITIZERS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g0 -O0 -fno-inline")
endif()

# GCC 的 ASAN detect_stack_use_after_scope 与 C++20 协程 lambda 有已知误报：
# lambda 闭包是栈上临时对象，在完整表达式结束后被 ASAN 标记为失效，
# 但协程帧（堆上）已拷贝了闭包。此处禁用该编译期插桩以消除误报。
# 堆 UAF / 缓冲区溢出等其它 ASAN 检查不受影响。
if(FLOWCORO_ENABLE_SANITIZERS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-sanitize-address-use-after-scope")
endif()

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找必要的库
find_package(Threads REQUIRED)

# 核心测试 - 只保留基础功能测试
add_executable(test_core test_core.cpp)
target_link_libraries(test_core flowcoro_net Threads::Threads)

# 新功能测试 - Channel通信
add_executable(test_new_features test_new_features.cpp)
target_link_libraries(test_new_features flowcoro_net Threads::Threads)

# 网络模块测试
add_executable(test_network test_network.cpp)

# 数据库模块测试
add_executable(test_database test_database.cpp)

# RPC模块测试
add_executable(test_rpc test_rpc.cpp)

# 跨线程协程测试
add_executable(test_cross_thread test_cross_thread.cpp)
target_link_libraries(test_cross_thread flowcoro_net Threads::Threads)

# when_any功能测试
add_executable(test_when_any test_when_any.cpp)

# sleep_for专项测试
add_executable(test_sleep_for test_sleep_for.cpp)

# 异常传播测试
add_executable(test_exception_propagation test_exception_propagation.cpp)

# 网络模块专业测试套件
add_executable(test_network_professional test_network_professional.cpp)

# BusAwaitable集成层测试（覆盖Bug 1/2/3修复）
add_executable(test_bus_awaitable test_bus_awaitable.cpp)

# ============================================================================
# 并发原语修复回归测试（hazard pointer / lockfree / when_any v2 / 取消 / 超时）
# ============================================================================

# lockfree Queue/Stack 压力测试（hazard pointer SMR 回归覆盖）
add_executable(test_lockfree_stress test_lockfree_stress.cpp)

# 事件驱动 when_any 测试（取代原 1ms 轮询实现）
add_executable(test_when_any_v2 test_when_any_v2.cpp)

# CancellationToken / CancellationTokenSource / 取消点 回归测试
add_executable(test_cancellation test_cancellation.cpp)

# with_timeout 可组合超时回归测试
add_executable(test_timeout test_timeout.cpp)

# C ABI 冒烟测试（纯 C 编译，验证 C 接口可被 C 项目直接调用）
add_executable(test_c_api test_c_api.c)
target_link_libraries(test_c_api flowcoro_net Threads::Threads)

# ============================================================================
# flowcoro::rt 确定性实时执行模型测试（单线程亲和 / post_ready / 两段式拆除）
# ============================================================================
add_executable(test_rt_executor test_rt_executor.cpp)

# 链接库
target_link_libraries(test_core
    flowcoro_net
    Threads::Threads
)

# 网络模块测试链接库
target_link_libraries(test_network
    flowcoro_net
    Threads::Threads
)

# 数据库模块测试链接库
target_link_libraries(test_database
    flowcoro_net
    Threads::Threads
)

# RPC模块测试链接库
target_link_libraries(test_rpc
    flowcoro_net
    Threads::Threads
)

# 跨线程协程测试链接库
target_link_libraries(test_cross_thread
    flowcoro_net
    Threads::Threads
)

# when_any功能测试链接库
target_link_libraries(test_when_any
    flowcoro_net
    Threads::Threads
)

# sleep_for专项测试链接库
target_link_libraries(test_sleep_for
    flowcoro_net
    Threads::Threads
)

# 异常传播测试链接库
target_link_libraries(test_exception_propagation
    flowcoro_net
    Threads::Threads
)

# 网络模块专业测试链接库
target_link_libraries(test_network_professional
    flowcoro_net
    Threads::Threads
)

# BusAwaitable测试链接库
target_link_libraries(test_bus_awaitable
    flowcoro_net
    Threads::Threads
)

# 并发原语回归测试链接库（统一依赖 flowcoro_net + Threads）
target_link_libraries(test_lockfree_stress
    flowcoro_net
    Threads::Threads
)

target_link_libraries(test_when_any_v2
    flowcoro_net
    Threads::Threads
)

target_link_libraries(test_cancellation
    flowcoro_net
    Threads::Threads
)

target_link_libraries(test_timeout
    flowcoro_net
    Threads::Threads
)

# flowcoro::rt 测试链接库
target_link_libraries(test_rt_executor
    flowcoro_net
    Threads::Threads
)

# 添加测试
add_test(NAME test_core COMMAND test_core)
add_test(NAME test_new_features COMMAND test_new_features)
add_test(NAME test_network COMMAND test_network)
add_test(NAME test_database COMMAND test_database)
add_test(NAME test_rpc COMMAND test_rpc)
add_test(NAME test_cross_thread COMMAND test_cross_thread)
add_test(NAME test_when_any COMMAND test_when_any)
add_test(NAME test_sleep_for COMMAND test_sleep_for)
add_test(NAME test_exception_propagation COMMAND test_exception_propagation)
add_test(NAME test_network_professional COMMAND test_network_professional)
add_test(NAME test_bus_awaitable      COMMAND test_bus_awaitable)
add_test(NAME test_lockfree_stress   COMMAND test_lockfree_stress)
add_test(NAME test_when_any_v2       COMMAND test_when_any_v2)
add_test(NAME test_cancellation      COMMAND test_cancellation)
add_test(NAME test_timeout           COMMAND test_timeout)
add_test(NAME test_c_api             COMMAND test_c_api)
add_test(NAME test_rt_executor       COMMAND test_rt_executor)

# 设置测试属性
set_tests_properties(test_core PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_new_features PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# test_network 发起真实外部 HTTP 请求 (http://httpbin.org/get)，在沙箱/CI
# 环境中会因无法访问外网而 hang。标记 LABELS 以便 CI 用 --label-exclude 排除。
set_tests_properties(test_network PROPERTIES
    TIMEOUT 60
    LABELS "network_external"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_database PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_rpc PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_cross_thread PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_when_any PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_sleep_for PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_exception_propagation PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_network_professional PROPERTIES
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_bus_awaitable PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 并发原语回归测试属性（lockfree 压力 + TSAN 友好）
set_tests_properties(test_lockfree_stress PROPERTIES
    TIMEOUT 180
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_when_any_v2 PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_cancellation PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_timeout PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_c_api PROPERTIES
    TIMEOUT 30
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_rt_executor PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置输出目录
set_target_properties(test_core test_new_features test_network test_database test_rpc test_cross_thread test_when_any test_sleep_for test_exception_propagation test_network_professional test_bus_awaitable test_lockfree_stress test_when_any_v2 test_cancellation test_timeout test_c_api test_rt_executor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

message(STATUS "FlowCoro core test configured successfully")
message(STATUS "Test executable will be built in: ${CMAKE_BINARY_DIR}/tests")
