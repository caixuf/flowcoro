# FlowCoro Benchmarks

# 查找benchmark库
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BENCHMARK benchmark)
endif()

if(NOT BENCHMARK_FOUND)
    message(STATUS "Google Benchmark not found, building simple benchmarks")


    # 添加精确基准测试
    add_executable(accurate_benchmarks accurate_bench.cpp)
    target_link_libraries(accurate_benchmarks flowcoro_net pthread)
    target_include_directories(accurate_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include)
    
    # 添加QPS性能测试 - 稳定可靠的QPS基准测试
    add_executable(qps_benchmarks simple_qps_benchmark.cpp)
    target_link_libraries(qps_benchmarks flowcoro_net pthread)
    target_include_directories(qps_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include)

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
