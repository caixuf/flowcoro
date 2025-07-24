# FlowCoro Examples

# Hello World 示例 - 基础协程入门
add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE flowcoro_net)

# Hello World 并发测试程序 - 独立进程版本
add_executable(hello_world_concurrent hello_world_concurrent.cpp)
target_link_libraries(hello_world_concurrent PRIVATE flowcoro_net)
message(STATUS "FlowCoro examples configured successfully")
