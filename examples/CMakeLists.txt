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

# Future组合器测试 - v2.2新功能测试
add_executable(test_future_combinators test_future_combinators.cpp)
target_link_libraries(test_future_combinators PRIVATE flowcoro_net)

message(STATUS "FlowCoro examples configured successfully")
