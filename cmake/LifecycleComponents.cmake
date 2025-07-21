# FlowCoro协程生命周期管理组件的CMake配置
# 适用于测试和演示程序

cmake_minimum_required(VERSION 3.20)

# 查找GoogleTest（如果需要运行测试）
find_package(GTest QUIET)

# 生命周期管理演示程序
add_executable(lifecycle_integration_demo
    examples/lifecycle_integration_demo.cpp
)

target_link_libraries(lifecycle_integration_demo 
    PRIVATE 
    flowcoro
    pthread
)

target_include_directories(lifecycle_integration_demo 
    PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 设置C++20标准
target_compile_features(lifecycle_integration_demo PRIVATE cxx_std_20)

# 添加编译选项
target_compile_options(lifecycle_integration_demo PRIVATE
    -Wall -Wextra -Wpedantic
    -O2
    $<$<CONFIG:Debug>:-g -DDEBUG>
    $<$<CONFIG:Release>:-DNDEBUG>
)

# 如果找到了GoogleTest，编译测试
if(GTest_FOUND)
    # 生命周期管理基础测试
    add_executable(test_lifecycle_basic
        tests/test_lifecycle_basic.cpp
    )
    
    target_link_libraries(test_lifecycle_basic 
        PRIVATE 
        flowcoro
        GTest::gtest
        GTest::gtest_main
        pthread
    )
    
    target_include_directories(test_lifecycle_basic 
        PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    
    target_compile_features(test_lifecycle_basic PRIVATE cxx_std_20)
    
    target_compile_options(test_lifecycle_basic PRIVATE
        -Wall -Wextra -Wpedantic
        -g -DDEBUG
    )
    
    # 添加到测试套件
    add_test(NAME LifecycleBasicTests COMMAND test_lifecycle_basic)
    
    message(STATUS "GoogleTest found, lifecycle tests will be built")
else()
    message(STATUS "GoogleTest not found, skipping lifecycle tests")
endif()

# 安装演示程序
install(TARGETS lifecycle_integration_demo
    RUNTIME DESTINATION bin
    COMPONENT examples
)

# 可选：安装测试程序
if(GTest_FOUND)
    install(TARGETS test_lifecycle_basic
        RUNTIME DESTINATION bin
        COMPONENT tests
    )
endif()
