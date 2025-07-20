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

# 数据库连接池示例 (需要MySQL支持)
if(FLOWCORO_ENABLE_DATABASE AND MYSQL_FOUND)
    add_executable(db_connection_pool_example db_connection_pool_example.cpp)
    target_include_directories(db_connection_pool_example PRIVATE ${MYSQL_INCLUDE_DIR})
    target_link_libraries(db_connection_pool_example PRIVATE 
        flowcoro_net 
        ${MYSQL_LIBRARY}
    )
    
    # 数据库压力测试
    add_executable(db_stress_test db_stress_test.cpp)
    target_include_directories(db_stress_test PRIVATE ${MYSQL_INCLUDE_DIR})
    target_link_libraries(db_stress_test PRIVATE 
        flowcoro_net 
        ${MYSQL_LIBRARY}
    )
endif()

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

# Phase 2 简化演示 - 不依赖外部数据库
add_executable(phase2_simple_demo phase2_simple_demo.cpp)
target_link_libraries(phase2_simple_demo PRIVATE flowcoro_net)
