# FlowCoro Examples

# Hello World 示例 - 基础协程入门
add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE flowcoro_net cjson)
# 定义项目根路径宏，让程序能够动态获取构建目录
target_compile_definitions(hello_world PRIVATE 
    "PROJECT_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
    "PROJECT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
)

# Hello World 并发测试程序 - 独立进程版本
add_executable(hello_world_concurrent hello_world_concurrent.cpp)
target_link_libraries(hello_world_concurrent PRIVATE flowcoro_net cjson)

# 性能监控演示程序
add_executable(performance_monitor_demo performance_monitor_demo.cpp)
target_link_libraries(performance_monitor_demo PRIVATE flowcoro_net)

# TCP Echo Server 示例 - 展示网络协程用法
add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE flowcoro_net)

# 聊天服务器 - 展示多客户端协程处理
add_executable(chat_server chat_server.cpp)
target_link_libraries(chat_server PRIVATE flowcoro_net)

# 聊天客户端 - 连接指南
add_executable(chat_client chat_client.cpp)

message(STATUS "FlowCoro examples configured successfully")
