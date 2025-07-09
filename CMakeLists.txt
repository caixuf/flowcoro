cmake_minimum_required(VERSION 3.14)
project(FlowCoro VERSION 1.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必要的库
find_package(Threads REQUIRED)

# 添加头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 添加可执行文件
add_executable(flowcoro_tests test/coro_tests.cpp)

# 链接库
target_link_libraries(flowcoro_tests PRIVATE Threads::Threads)

# 启用协程支持
target_compile_options(flowcoro_tests PRIVATE -fcoroutines)

# 设置编译器特定的协程标志
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(flowcoro_tests PRIVATE -stdlib=libc++)
endif()
