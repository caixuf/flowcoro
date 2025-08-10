# FlowCoro Benchmarks

# 查找benchmark库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BENCHMARK benchmark)
endif()

if(NOT BENCHMARK_FOUND)
    message(STATUS "Google Benchmark not found, building simple benchmarks")

    # 添加简单基准测试 - 核心功能性能测试
    add_executable(simple_benchmark simple_benchmark.cpp)
    target_link_libraries(simple_benchmark flowcoro_net pthread)
    target_include_directories(simple_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(simple_benchmark PRIVATE cxx_std_20)

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
