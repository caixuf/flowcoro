# FlowCoro 统一测试配置
cmake_minimum_required(VERSION 3.16)

# 确保使用正确的 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项（内存优化版本）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines -pthread -g0 -O0 -fno-inline")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=128 -ftemplate-backtrace-limit=10")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector --param ggc-min-expand=10 --param ggc-min-heapsize=32768")

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 查找必要的库
find_package(Threads REQUIRED)

# 定义测试可执行文件（整理后版本）
set(TEST_SOURCES
    test_core.cpp
    test_database.cpp
    test_http_client.cpp
    test_simple_db.cpp
    test_rpc.cpp
    test_sleep_only.cpp
)

# 为每个测试文件创建单独的可执行文件
foreach(TEST_SOURCE ${TEST_SOURCES})
    # 获取测试名称（去除 .cpp 扩展名）
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    
    # 创建可执行文件
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    
    # 链接库
    target_link_libraries(${TEST_NAME} 
        flowcoro_net
        Threads::Threads
    )
    
    # 添加测试
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    
    # 设置测试属性
    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endforeach()

# 创建统一的测试运行器
add_executable(run_all_tests run_all_tests.cpp)
target_link_libraries(run_all_tests 
    flowcoro_net
    Threads::Threads
)

# 添加统一测试
add_test(NAME all_tests COMMAND run_all_tests)
set_tests_properties(all_tests PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 保留旧的测试（兼容性）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_simple.cpp)
    add_executable(flowcoro_legacy_tests test_simple.cpp)
    target_link_libraries(flowcoro_legacy_tests flowcoro_net Threads::Threads)
    add_test(NAME legacy_tests COMMAND flowcoro_legacy_tests)
endif()

# 如果存在 Phase2 测试
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_phase2.cpp)
    find_package(GTest QUIET)
    if(GTest_FOUND)
        add_executable(phase2_legacy_tests test_phase2.cpp)
        target_link_libraries(phase2_legacy_tests 
            flowcoro_net
            Threads::Threads
            GTest::GTest 
            GTest::Main
        )
        add_test(NAME phase2_legacy COMMAND phase2_legacy_tests)
    endif()
endif()

# 设置输出目录
set_target_properties(
    test_core test_database test_http_client test_simple_db test_rpc test_sleep_only run_all_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

message(STATUS "FlowCoro unified tests configured successfully")
message(STATUS "Test executables will be built in: ${CMAKE_BINARY_DIR}/tests")
