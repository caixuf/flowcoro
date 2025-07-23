# FlowCoro Examples

# Hello World 示例 - 基础协程入门
add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE flowcoro_net)

# Hello World 并发测试程序 - 独立进程版本
add_executable(hello_world_concurrent hello_world_concurrent.cpp)
target_link_libraries(hello_world_concurrent PRIVATE flowcoro_net)

# 增强示例 - 并发演示
add_executable(enhanced_demo enhanced_demo.cpp)
target_link_libraries(enhanced_demo PRIVATE flowcoro_net)

# 网络IO示例 - TCP服务器
add_executable(network_example network_example.cpp)
target_link_libraries(network_example PRIVATE flowcoro_net)

# 统一Task接口演示
add_executable(unified_task_demo unified_task_demo.cpp)
target_link_libraries(unified_task_demo PRIVATE flowcoro_net)

# 简洁的统一Task接口演示
add_executable(simple_unified_demo simple_unified_demo.cpp)
target_link_libraries(simple_unified_demo PRIVATE flowcoro_net)

# 协程池演示程序
add_executable(coroutine_pool_demo coroutine_pool_demo.cpp)
target_link_libraries(coroutine_pool_demo PRIVATE flowcoro_net)

# 安全的协程池演示程序
add_executable(safe_coroutine_pool_demo safe_coroutine_pool_demo.cpp)
target_link_libraries(safe_coroutine_pool_demo PRIVATE flowcoro_net)

# 简化协程池测试
add_executable(simple_pool_test simple_pool_test.cpp)
target_link_libraries(simple_pool_test PRIVATE flowcoro_net)

message(STATUS "FlowCoro examples configured successfully")
