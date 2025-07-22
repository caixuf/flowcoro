# FlowCoro Examples

# Hello World 示例 - 基础协程入门
add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE flowcoro_net)

# 增强示例 - 并发演示
add_executable(enhanced_example enhanced_demo.cpp)
target_link_libraries(enhanced_example PRIVATE flowcoro_net)

# 网络IO示例 - TCP服务器
add_executable(network_example network_example.cpp)
target_link_libraries(network_example PRIVATE flowcoro_net)

# 改进的SafeTask示例 - 基于async_simple最佳实践
add_executable(improved_safe_task_demo improved_safe_task_demo.cpp)
target_link_libraries(improved_safe_task_demo PRIVATE flowcoro_net)

message(STATUS "FlowCoro examples configured successfully")
