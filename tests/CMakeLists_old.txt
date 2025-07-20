# FlowCoro Tests - 精简版本

# 综合测试 (包含所有功能：无锁数据结构、协程、异步编程、网络请求等)
add_executable(flowcoro_tests test_simple.cpp)
target_link_libraries(flowcoro_tests PRIVATE flowcoro flowcoro_net)

# Phase 2 数据库层测试
if(FLOWCORO_ENABLE_DATABASE)
    # 查找Google Test
    find_package(GTest QUIET)
    if(GTest_FOUND OR TARGET GTest::gtest)
        add_executable(phase2_tests test_phase2.cpp)
        target_link_libraries(phase2_tests PRIVATE 
            flowcoro 
            flowcoro_net
            GTest::gtest 
            GTest::gtest_main
        )
        
        # 添加数据库依赖
        if(MYSQL_FOUND)
            target_include_directories(phase2_tests PRIVATE ${MYSQL_INCLUDE_DIR})
            target_link_libraries(phase2_tests PRIVATE ${MYSQL_LIBRARY})
        endif()
        
        if(HIREDIS_FOUND)
            target_include_directories(phase2_tests PRIVATE ${HIREDIS_INCLUDE_DIR})
            target_link_libraries(phase2_tests PRIVATE ${HIREDIS_LIBRARY})
        endif()
        
        if(JSONCPP_FOUND)
            target_include_directories(phase2_tests PRIVATE ${JSONCPP_INCLUDE_DIR})
            target_link_libraries(phase2_tests PRIVATE ${JSONCPP_LIBRARY})
        endif()
        
        # 添加到测试套件
        add_test(NAME Phase2DatabaseTests COMMAND phase2_tests)
        set_tests_properties(Phase2DatabaseTests PROPERTIES
            TIMEOUT 120
            LABELS "database;redis;mysql;transaction;monitoring"
        )
    else()
        message(WARNING "Google Test not found, Phase 2 tests will be skipped")
    endif()
endif()

# 添加到测试套件
add_test(NAME ComprehensiveTests COMMAND flowcoro_tests)

# 设置测试属性
set_tests_properties(ComprehensiveTests PROPERTIES
    TIMEOUT 60
    LABELS "comprehensive;async;lockfree;coroutine;network"
)
