cmake_minimum_required(VERSION 3.16)

# 项目信息
project(FlowCoro
    VERSION 4.0.0
    DESCRIPTION "Modern C++20 Coroutine Library - Streamlined Core Implementation"
    LANGUAGES C CXX
)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release: 全力优化，不保留帧指针（让寄存器分配器自由使用 rbp）
set(CMAKE_CXX_FLAGS_RELEASE
    "-O3 -DNDEBUG -march=native -mtune=native -flto=auto -ffast-math -funroll-loops -fno-plt -fomit-frame-pointer"
    CACHE STRING "Release build flags" FORCE)

# RelWithDebInfo: 保留调试信息，适合 valgrind/callgrind
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "-O2 -g -DNDEBUG -march=native -mtune=native -fno-omit-frame-pointer"
    CACHE STRING "RelWithDebInfo build flags" FORCE)

# Profiling: 专用性能分析构建（Release 级别 + 帧指针 + 行号 + 关闭内联以便 perf/callgrind 看到真实调用栈）
set(CMAKE_CXX_FLAGS_PROFILING
    "-O2 -g -DNDEBUG -march=native -mtune=native -fno-omit-frame-pointer -fno-inline-functions-called-once"
    CACHE STRING "Profiling build flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_PROFILING "-fno-omit-frame-pointer"
    CACHE STRING "Profiling linker flags" FORCE)

# LTO 链接器参数（Release/Profiling 时由 -flto=auto 驱动，链接器也需要同步）
set(CMAKE_EXE_LINKER_FLAGS_RELEASE    "-flto=auto -Wl,-O1 -Wl,--as-needed"
    CACHE STRING "Release linker flags" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "-flto=auto -Wl,-O1 -Wl,--as-needed"
    CACHE STRING "Release shared linker flags" FORCE)

# PGO (Profile-Guided Optimization) 自动支持
option(FLOWCORO_ENABLE_PGO "Enable Profile-Guided Optimization" ON)
option(FLOWCORO_PGO_GENERATE "Generate PGO profile data" OFF)

# 自动检测并启用PGO优化
if(FLOWCORO_ENABLE_PGO)
    if(FLOWCORO_PGO_GENERATE)
        message(STATUS "Enabling PGO profile generation")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-generate")
        set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fprofile-generate")
        set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -fprofile-generate")
    else()
        # 查找已存在的profile数据
        file(GLOB_RECURSE GCDA_FILES 
             "${CMAKE_CURRENT_SOURCE_DIR}/pgo_profiles/*.gcda")
             
        if(GCDA_FILES)
            list(LENGTH GCDA_FILES PROFILE_COUNT)
            message(STATUS "Found ${PROFILE_COUNT} PGO profile files, enabling PGO optimization")
            
            # 复制profile到构建目录
            file(COPY ${GCDA_FILES} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
            
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fprofile-use -fprofile-correction")
            set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} -fprofile-use")
            set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} -fprofile-use")
        else()
            message(STATUS "No PGO profile data found. Build without PGO optimization.")
            message(STATUS "To enable PGO: First build with -DFLOWCORO_PGO_GENERATE=ON, run workloads, then rebuild normally")
        endif()
    endif()
endif()

# 设置默认构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
# 让 cmake-gui / ccmake 显示可选列表
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "RelWithDebInfo" "Profiling" "MinSizeRel")

# 项目选项
option(FLOWCORO_BUILD_TESTS "Build tests" ON)
option(FLOWCORO_BUILD_EXAMPLES "Build examples" ON)
option(FLOWCORO_BUILD_BENCHMARKS "Build benchmarks" ON)
option(FLOWCORO_BUILD_DOCS "Build documentation" OFF)
option(FLOWCORO_ENABLE_SANITIZERS "Enable sanitizers (address+undefined)" OFF)
option(FLOWCORO_ENABLE_TSAN "Enable ThreadSanitizer (mutually exclusive with ASAN)" OFF)
option(FLOWCORO_BUILD_V3 "Build FlowCoro 3.0 architecture" ON)
option(FLOWCORO_ENABLE_DATABASE "Enable database support" ON)

# 线程池大小（0 = 运行时自动检测 hardware_concurrency）
# 对于混合架构 CPU（如 Alder Lake），可手动设为物理 P-core 数量（通常 = 逻辑核数 / 2）
set(FLOWCORO_THREAD_POOL_SIZE 0 CACHE STRING
    "Default thread pool size (0=auto, recommend P-core count for hybrid CPUs)")

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
    src/c_api.cpp
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
)

# 添加项目路径宏定义
target_compile_definitions(flowcoro_net PUBLIC
    "PROJECT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\""
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
# 注意：-fno-omit-frame-pointer 仅在非 Release 构建时启用
# Release 模式下让 -O3 + -fomit-frame-pointer 释放 rbp 寄存器
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(flowcoro INTERFACE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        # Release 以外的构建保留帧指针，方便调试/profiling
        $<$<NOT:$<CONFIG:Release>>:-fno-omit-frame-pointer>
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(flowcoro INTERFACE
        /W4 /permissive-
    )
endif()

# 将线程池大小编译进库（若非 0）
if(FLOWCORO_THREAD_POOL_SIZE GREATER 0)
    target_compile_definitions(flowcoro_net PUBLIC
        FLOWCORO_THREAD_POOL_SIZE=${FLOWCORO_THREAD_POOL_SIZE}
    )
    message(STATUS "FlowCoro: fixed thread pool size = ${FLOWCORO_THREAD_POOL_SIZE}")
else()
    message(STATUS "FlowCoro: thread pool size = auto (hardware_concurrency)")
endif()

# Sanitizers支持
# - FLOWCORO_ENABLE_SANITIZERS: AddressSanitizer + UndefinedBehaviorSanitizer
# - FLOWCORO_ENABLE_TSAN:        ThreadSanitizer（与 ASAN 互斥，CI 用于检测数据竞争）
#
# TSAN 不能与 ASAN 同时启用；若两者都被开启，TSAN 优先（用于线程安全回归测试）。
if(FLOWCORO_ENABLE_TSAN)
    if(FLOWCORO_ENABLE_SANITIZERS)
        message(WARNING "FLOWCORO_ENABLE_TSAN and FLOWCORO_ENABLE_SANITIZERS are both ON; "
                         "ASAN/UBSAN will be suppressed in favor of TSAN.")
    endif()
    message(STATUS "FlowCoro: ThreadSanitizer enabled (-fsanitize=thread)")
    target_compile_options(flowcoro INTERFACE
        -fsanitize=thread
        -fno-sanitize-recover=thread
        -g
        -O1
        $<$<NOT:$<CONFIG:Release>>:-fno-omit-frame-pointer>
    )
    target_compile_options(flowcoro_net PUBLIC
        -fsanitize=thread
        -fno-sanitize-recover=thread
        -g
        -O1
        $<$<NOT:$<CONFIG:Release>>:-fno-omit-frame-pointer>
    )
    target_link_options(flowcoro INTERFACE -fsanitize=thread)
    target_link_options(flowcoro_net PUBLIC  -fsanitize=thread)
elseif(FLOWCORO_ENABLE_SANITIZERS)
    message(STATUS "FlowCoro: ASAN+UBSAN enabled (-fsanitize=address,undefined)")
    target_compile_options(flowcoro INTERFACE
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -g
        $<$<NOT:$<CONFIG:Release>>:-fno-omit-frame-pointer>
    )
    target_compile_options(flowcoro_net PUBLIC
        -fsanitize=address,undefined
        -fno-sanitize-recover=all
        -g
        $<$<NOT:$<CONFIG:Release>>:-fno-omit-frame-pointer>
    )
    target_link_options(flowcoro INTERFACE -fsanitize=address,undefined)
    target_link_options(flowcoro_net PUBLIC  -fsanitize=address,undefined)
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
