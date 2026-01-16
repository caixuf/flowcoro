# FlowCoro Examples

# Hello World  - 
add_executable(hello_world hello_world.cpp)
target_link_libraries(hello_world PRIVATE flowcoro_net)
# 
target_compile_definitions(hello_world PRIVATE 
    "PROJECT_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
    "PROJECT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
)

# Hello World  - 
add_executable(hello_world_concurrent hello_world_concurrent.cpp)
target_link_libraries(hello_world_concurrent PRIVATE flowcoro_net)

# 
add_executable(performance_monitor_demo performance_monitor_demo.cpp)
target_link_libraries(performance_monitor_demo PRIVATE flowcoro_net)

# TCP Echo Server  - 
add_executable(echo_server echo_server.cpp)
target_link_libraries(echo_server PRIVATE flowcoro_net)

#  - 
add_executable(chat_server chat_server.cpp)
target_link_libraries(chat_server PRIVATE flowcoro_net)

#  - 
add_executable(chat_client chat_client.cpp)

message(STATUS "FlowCoro examples configured successfully")
