# FlowCoro Benchmarks

# benchmark
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(BENCHMARK benchmark)
endif()

if(NOT BENCHMARK_FOUND)
    message(STATUS "Google Benchmark not found, building professional benchmarks")

    #  - 
    add_executable(professional_flowcoro_benchmark professional_flowcoro_benchmark.cpp)
    target_link_libraries(professional_flowcoro_benchmark flowcoro_net pthread)
    target_include_directories(professional_flowcoro_benchmark PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_compile_features(professional_flowcoro_benchmark PRIVATE cxx_std_20)

else()
    message(STATUS "Found Google Benchmark, building full benchmarks")

    # 
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
