# FlowCoro Examples

# 查找 cJSON 库（可选依赖）
find_path(CJSON_INCLUDE_DIR cjson/cJSON.h
    PATHS /usr/include /usr/local/include /opt/homebrew/include
)
find_library(CJSON_LIBRARY
    NAMES cjson
    PATHS /usr/lib /usr/local/lib /opt/homebrew/lib
)

if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
    set(CJSON_FOUND TRUE)
    message(STATUS "Found cJSON: ${CJSON_LIBRARY}")
else()
    set(CJSON_FOUND FALSE)
    message(WARNING "cJSON not found, hello_world and hello_world_concurrent examples will be disabled")
endif()

# Hello World 示例 - 基础协程入门（依赖 cJSON）
if(CJSON_FOUND)
    add_executable(hello_world hello_world.cpp)
    target_include_directories(hello_world PRIVATE ${CJSON_INCLUDE_DIR})
    target_link_libraries(hello_world PRIVATE flowcoro_net ${CJSON_LIBRARY})
    # 定义项目根路径宏，让程序能够动态获取构建目录
    target_compile_definitions(hello_world PRIVATE
        "PROJECT_BUILD_DIR=\"${CMAKE_BINARY_DIR}\""
        "PROJECT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
    )

    # Hello World 并发测试程序 - 独立进程版本（依赖 cJSON）
    add_executable(hello_world_concurrent hello_world_concurrent.cpp)
    target_include_directories(hello_world_concurrent PRIVATE ${CJSON_INCLUDE_DIR})
    target_link_libraries(hello_world_concurrent PRIVATE flowcoro_net ${CJSON_LIBRARY})
endif()

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
