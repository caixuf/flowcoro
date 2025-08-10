cmake_minimum_required(VERSION 3.16)

# 项目信息
project(FlowCoro
    VERSION 4.0.0
    DESCRIPTION "Modern C++20 Coroutine Library - Streamlined Core Implementation"
    LANGUAGES CXX
)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -DNDEBUG")

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# 项目选项
option(FLOWCORO_BUILD_TESTS "Build tests" ON)
option(FLOWCORO_BUILD_EXAMPLES "Build examples" ON)
option(FLOWCORO_BUILD_BENCHMARKS "Build benchmarks" ON)
option(FLOWCORO_BUILD_DOCS "Build documentation" OFF)
option(FLOWCORO_ENABLE_SANITIZERS "Enable sanitizers" OFF)
option(FLOWCORO_BUILD_V3 "Build FlowCoro 3.0 architecture" ON)
option(FLOWCORO_ENABLE_DATABASE "Enable database support" ON)

# 查找依赖
find_package(Threads REQUIRED)

# 数据库支持
if(FLOWCORO_ENABLE_DATABASE)
    # 查找MySQL客户端库
    find_path(MYSQL_INCLUDE_DIR mysql/mysql.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        PATH_SUFFIXES mysql
    )

    find_library(MYSQL_LIBRARY
        NAMES mysqlclient mysql libmysql
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        PATH_SUFFIXES mysql
    )

    if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
        set(MYSQL_FOUND TRUE)
        message(STATUS "Found MySQL: ${MYSQL_LIBRARY}")
    else()
        set(MYSQL_FOUND FALSE)
        message(WARNING "MySQL not found, database examples will be disabled")
    endif()

    # 查找Redis客户端库 (hiredis)
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
    )

    find_library(HIREDIS_LIBRARY
        NAMES hiredis
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
    )

    if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
        set(HIREDIS_FOUND TRUE)
        message(STATUS "Found hiredis: ${HIREDIS_LIBRARY}")
    else()
        set(HIREDIS_FOUND FALSE)
        message(WARNING "hiredis not found, Redis examples will be disabled")
    endif()

    # 查找JSON库 (jsoncpp)
    find_path(JSONCPP_INCLUDE_DIR json/json.h
        PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
        PATH_SUFFIXES jsoncpp
    )

    find_library(JSONCPP_LIBRARY
        NAMES jsoncpp
        PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
    )

    if(JSONCPP_INCLUDE_DIR AND JSONCPP_LIBRARY)
        set(JSONCPP_FOUND TRUE)
        message(STATUS "Found jsoncpp: ${JSONCPP_LIBRARY}")
    else()
        set(JSONCPP_FOUND FALSE)
        message(WARNING "jsoncpp not found, monitoring features will be limited")
    endif()
endif()

# 包含cmake模块
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# 头文件目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# =============================================================================
# FlowCoro核心库 (Header-Only)
# =============================================================================

add_library(flowcoro INTERFACE)

target_include_directories(flowcoro INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# =============================================================================
# FlowCoro网络库 (实现库)
# =============================================================================

add_library(flowcoro_net STATIC
    src/net_impl.cpp
    src/globals.cpp
    src/coroutine_pool.cpp
)

target_include_directories(flowcoro_net PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(flowcoro INTERFACE cxx_std_20)
target_compile_features(flowcoro_net PUBLIC cxx_std_20)

# 链接系统库
target_link_libraries(flowcoro INTERFACE
    Threads::Threads
    $<$<PLATFORM_ID:Linux>:atomic>
)

target_link_libraries(flowcoro_net PUBLIC
    flowcoro
    Threads::Threads
    $<$<PLATFORM_ID:Linux>:atomic>
    cjson
)

# 协程支持
target_compile_options(flowcoro INTERFACE
    $<$<CXX_COMPILER_ID:GNU>:-fcoroutines>
    $<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>
)

target_compile_options(flowcoro_net PUBLIC
    $<$<CXX_COMPILER_ID:GNU>:-fcoroutines>
    $<$<CXX_COMPILER_ID:Clang>:-stdlib=libc++>
)

# 编译器特定选项
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(flowcoro INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -fno-omit-frame-pointer
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(flowcoro INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        -fno-omit-frame-pointer
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(flowcoro INTERFACE
        /W4 /permissive-
    )
endif()

# Sanitizers支持
if(FLOWCORO_ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(flowcoro INTERFACE
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
    )
    target_link_options(flowcoro INTERFACE
        -fsanitize=address,undefined
    )
endif()

# =============================================================================
# 测试
# =============================================================================

if(FLOWCORO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# =============================================================================
# 示例
# =============================================================================

if(FLOWCORO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# =============================================================================
# 基准测试
# =============================================================================

if(FLOWCORO_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# =============================================================================
# FlowCoro 3.0 架构
# =============================================================================

# V3 架构已经集成到核心中，不再需要单独的文件

# =============================================================================
# 文档
# =============================================================================

if(FLOWCORO_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        add_subdirectory(docs)
    else()
        message(WARNING "Doxygen not found, documentation will not be built")
    endif()
endif()

# =============================================================================
# 安装配置
# =============================================================================

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# 安装头文件
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# 安装目标
install(TARGETS flowcoro
    EXPORT FlowCoroTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# 生成配置文件
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FlowCoroConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/FlowCoroConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowCoro
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/FlowCoroConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# 导出目标
export(EXPORT FlowCoroTargets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/FlowCoroTargets.cmake"
    NAMESPACE FlowCoro::
)

# 安装配置文件
install(EXPORT FlowCoroTargets
    FILE FlowCoroTargets.cmake
    NAMESPACE FlowCoro::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowCoro
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/FlowCoroConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/FlowCoroConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FlowCoro
)

# =============================================================================
# 打包配置
# =============================================================================

set(CPACK_PACKAGE_NAME "FlowCoro")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_VENDOR "FlowCoro Team")
set(CPACK_PACKAGE_CONTACT "flowcoro@example.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

include(CPack)

# =============================================================================
# 信息输出
# =============================================================================

message(STATUS "")
message(STATUS "FlowCoro ${PROJECT_VERSION} Configuration Summary:")
message(STATUS " Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS " Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS " Build tests: ${FLOWCORO_BUILD_TESTS}")
message(STATUS " Build examples: ${FLOWCORO_BUILD_EXAMPLES}")
message(STATUS " Build benchmarks: ${FLOWCORO_BUILD_BENCHMARKS}")
message(STATUS " Build docs: ${FLOWCORO_BUILD_DOCS}")
message(STATUS " Build V3 architecture: ${FLOWCORO_BUILD_V3}")
message(STATUS " Enable sanitizers: ${FLOWCORO_ENABLE_SANITIZERS}")
message(STATUS " Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
