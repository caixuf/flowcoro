# FlowCoro Benchmarks

# 查找benchmark库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BENCHMARK benchmark)
endif()

if(NOT BENCHMARK_FOUND)
    message(STATUS "Google Benchmark not found, building professional benchmarks")

    # 添加专业基准测试 - 核心功能性能测试
    add_executable(professional_flowcoro_benchmark professional_flowcoro_benchmark.cpp)
    target_link_libraries(professional_flowcoro_benchmark flowcoro_net pthread)
    target_include_directories(professional_flowcoro_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(professional_flowcoro_benchmark PRIVATE cxx_std_20)

else()
    message(STATUS "Found Google Benchmark, building full benchmarks")

    # 完整基准测试
    add_executable(full_benchmarks
        full_bench.cpp
        coroutine_bench.cpp
        memory_bench.cpp
        lockfree_bench.cpp
    )
    target_link_libraries(full_benchmarks PRIVATE
        flowcoro_net
        ${BENCHMARK_LIBRARIES}
    )
    target_include_directories(full_benchmarks PRIVATE ${BENCHMARK_INCLUDE_DIRS})
    target_compile_options(full_benchmarks PRIVATE ${BENCHMARK_CFLAGS_OTHER})
endif()

# Honest real-performance binaries (always, when FLOWCORO_BUILD_BENCHMARKS=ON).
# These are not the CPU-sim Echo/HTTP rows in professional_flowcoro_benchmark.
find_package(Threads REQUIRED)

add_executable(real_net_benchmark real_net_benchmark.cpp)
target_link_libraries(real_net_benchmark PRIVATE flowcoro_net Threads::Threads)
target_include_directories(real_net_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(real_net_benchmark PRIVATE cxx_std_20)

add_executable(rt_cycle_jitter_benchmark rt_cycle_jitter_benchmark.cpp)
target_link_libraries(rt_cycle_jitter_benchmark PRIVATE flowcoro_net Threads::Threads)
target_include_directories(rt_cycle_jitter_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(rt_cycle_jitter_benchmark PRIVATE cxx_std_20)
