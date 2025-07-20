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
