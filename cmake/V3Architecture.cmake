# FlowCoro 3.0 架构 CMake 配置
cmake_minimum_required(VERSION 3.20)

# 创建 v3 测试可执行文件
add_executable(test_v3_architecture 
    tests/test_v3_architecture.cpp
    src/v3_core.cpp
)

# 设置 C++ 标准
target_compile_features(test_v3_architecture PRIVATE cxx_std_20)

# 设置编译选项
target_compile_options(test_v3_architecture PRIVATE
    -Wall -Wextra -O2
    -fcoroutines
    -pthread
)

# 包含目录
target_include_directories(test_v3_architecture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/flowcoro
)

# 链接库
target_link_libraries(test_v3_architecture PRIVATE
    pthread
)

# 为 v3 架构添加特定的编译定义
target_compile_definitions(test_v3_architecture PRIVATE
    FLOWCORO_V3_ENABLED=1
    FLOWCORO_STACK_SIZE=65536
    FLOWCORO_MAX_POOL_SIZE=1000
)

# 创建一个性能基准测试
add_executable(v3_benchmark
    tests/test_v3_architecture.cpp
    src/v3_core.cpp
)

target_compile_features(v3_benchmark PRIVATE cxx_std_20)
target_compile_options(v3_benchmark PRIVATE
    -Wall -Wextra -O3 -DNDEBUG
    -fcoroutines
    -pthread
    -march=native
)

target_include_directories(v3_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/flowcoro
)

target_link_libraries(v3_benchmark PRIVATE
    pthread
)

target_compile_definitions(v3_benchmark PRIVATE
    FLOWCORO_V3_ENABLED=1
    FLOWCORO_STACK_SIZE=65536
    FLOWCORO_MAX_POOL_SIZE=2000
    FLOWCORO_BENCHMARK_MODE=1
)

# 创建栈池真实使用测试
add_executable(test_stack_pool_usage
    tests/test_stack_pool_usage.cpp
    src/v3_core.cpp
)

target_compile_features(test_stack_pool_usage PRIVATE cxx_std_20)
target_compile_options(test_stack_pool_usage PRIVATE
    -Wall -Wextra -O2
    -fcoroutines
    -pthread
)

target_include_directories(test_stack_pool_usage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/flowcoro
)

target_link_libraries(test_stack_pool_usage PRIVATE
    pthread
)

target_compile_definitions(test_stack_pool_usage PRIVATE
    FLOWCORO_V3_ENABLED=1
    FLOWCORO_STACK_SIZE=65536
    FLOWCORO_MAX_POOL_SIZE=1000
)

# 安装目标（可选）
install(TARGETS test_v3_architecture v3_benchmark test_stack_pool_usage
    RUNTIME DESTINATION bin
)

# 测试配置
enable_testing()
add_test(NAME "FlowCoro_V3_Architecture_Test" 
         COMMAND test_v3_architecture)

add_test(NAME "FlowCoro_V3_Benchmark" 
         COMMAND v3_benchmark)

add_test(NAME "FlowCoro_V3_StackPool_Usage_Test" 
         COMMAND test_stack_pool_usage)
