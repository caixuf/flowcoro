# FlowCoro 核心测试配置
cmake_minimum_required(VERSION 3.16)

# 确保使用正确的 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项（内存优化版本）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines -pthread -g0 -O0 -fno-inline")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=128 -ftemplate-backtrace-limit=10")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector --param ggc-min-expand=10 --param ggc-min-heapsize=32768")

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找必要的库
find_package(Threads REQUIRED)

# 核心测试 - 只保留基础功能测试
add_executable(test_core test_core.cpp)
target_link_libraries(test_core flowcoro_net Threads::Threads)

# 新功能测试 - Channel和异步同步原语
add_executable(test_new_features test_new_features.cpp)
target_link_libraries(test_new_features flowcoro_net Threads::Threads)

# Channel独立测试 - 已删除
# add_executable(test_channel_only test_channel_only.cpp)
# target_link_libraries(test_channel_only flowcoro_net Threads::Threads)

# 调试协程行为测试 - 已删除
# add_executable(debug_coroutine debug_coroutine.cpp)
# target_link_libraries(debug_coroutine flowcoro_net Threads::Threads)

# 异步同步原语完整测试 - 合并到 test_new_features 中
# add_executable(test_async_sync test_async_sync.cpp)
# target_link_libraries(test_async_sync flowcoro_net Threads::Threads)

# AsyncSemaphore测试已合并到 test_new_features 中

# 网络模块测试
add_executable(test_network test_network.cpp)

# 数据库模块测试
add_executable(test_database test_database.cpp)

# RPC模块测试
add_executable(test_rpc test_rpc.cpp)

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

# 添加测试
add_test(NAME test_core COMMAND test_core)
add_test(NAME test_new_features COMMAND test_new_features)
add_test(NAME test_network COMMAND test_network)
add_test(NAME test_database COMMAND test_database)
add_test(NAME test_rpc COMMAND test_rpc)

# 设置测试属性
set_tests_properties(test_core PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_new_features PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_network PROPERTIES
    TIMEOUT 60
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

# 设置输出目录
set_target_properties(test_core test_new_features test_network test_database test_rpc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

message(STATUS "FlowCoro core test configured successfully")
message(STATUS "Test executable will be built in: ${CMAKE_BINARY_DIR}/tests")
