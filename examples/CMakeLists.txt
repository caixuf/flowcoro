# FlowCoro Examples

# 基础示例
add_executable(basic_example example.cpp)
target_link_libraries(basic_example PRIVATE flowcoro_net)

# 增强示例
add_executable(enhanced_example enhanced_demo.cpp)
target_link_libraries(enhanced_example PRIVATE flowcoro_net)

# 网络IO示例
add_executable(network_example network_example.cpp)
target_link_libraries(network_example PRIVATE flowcoro_net)

# Phase 2简化示例 (低内存版本)
add_executable(phase2_simple phase2_simple.cpp)
target_link_libraries(phase2_simple PRIVATE flowcoro_net)

# HTTP + Database 集成演示
add_executable(http_db_integration_demo http_db_integration_demo.cpp)
target_link_libraries(http_db_integration_demo PRIVATE flowcoro_net)

# RPC系统演示
add_executable(rpc_demo rpc_demo.cpp)
target_link_libraries(rpc_demo PRIVATE flowcoro_net)

# 轻量级RPC演示（内存友好）
add_executable(simple_rpc_demo simple_rpc_demo.cpp)
target_link_libraries(simple_rpc_demo PRIVATE flowcoro_net)

# 数据库集成示例
add_executable(simple_integration_demo simple_integration_demo.cpp)
target_link_libraries(simple_integration_demo PRIVATE flowcoro_net)


# Phase 2 演示程序 (完整的数据访问层功能)
if(FLOWCORO_ENABLE_DATABASE)
    add_executable(phase2_demo phase2_demo.cpp)
    target_link_libraries(phase2_demo PRIVATE flowcoro_net)
    
    # 添加MySQL支持
    if(MYSQL_FOUND)
        target_include_directories(phase2_demo PRIVATE ${MYSQL_INCLUDE_DIR})
        target_link_libraries(phase2_demo PRIVATE ${MYSQL_LIBRARY})
        target_compile_definitions(phase2_demo PRIVATE FLOWCORO_HAS_MYSQL=1)
    endif()
    
    # 添加Redis支持
    if(HIREDIS_FOUND)
        target_include_directories(phase2_demo PRIVATE ${HIREDIS_INCLUDE_DIR})
        target_link_libraries(phase2_demo PRIVATE ${HIREDIS_LIBRARY})
        target_compile_definitions(phase2_demo PRIVATE FLOWCORO_HAS_REDIS=1)
    endif()
    
    # 添加JSON支持
    if(JSONCPP_FOUND)
        target_include_directories(phase2_demo PRIVATE ${JSONCPP_INCLUDE_DIR})
        target_link_libraries(phase2_demo PRIVATE ${JSONCPP_LIBRARY})
        target_compile_definitions(phase2_demo PRIVATE FLOWCORO_HAS_JSON=1)
    endif()
endif()

message(STATUS "FlowCoro examples configured successfully")
