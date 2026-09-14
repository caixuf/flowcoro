# =============================================================================
# flowcoro_py —— Python 绑定（pybind11）
#
# 只链接 INTERFACE 目标 flowcoro。绑定用到的实现全部在 header-only 部分
# （thread_pool.h / lockfree.h / memory_pool.h / hazard_pointer.h /
#  bounded_channel.h / cpu_affinity.h），src/globals.cpp 与 src/c_api.cpp 里的
# 符号一个都不需要。因此**不需要**给 flowcoro_net 打开 -fPIC，也不会和
# 静态库/LTO 产生交互。
# =============================================================================

include(FetchContent)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    if(NOT FLOWCORO_PYTHON_FETCH_PYBIND11)
        message(FATAL_ERROR
            "未找到 pybind11，且 FLOWCORO_PYTHON_FETCH_PYBIND11=OFF。\n"
            "请任选其一：\n"
            "  1) pip install pybind11，然后重新配置并加上\n"
            "     -Dpybind11_DIR=\"$(python3 -m pybind11 --cmakedir)\"\n"
            "  2) 保持 -DFLOWCORO_PYTHON_FETCH_PYBIND11=ON（默认）让 CMake 自动拉取")
    endif()
    message(STATUS "pybind11 not found locally, fetching v2.13.6 via FetchContent")
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG v2.13.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(pybind11)
endif()

add_library(flowcoro_py MODULE flowcoro_py.cpp)

target_link_libraries(flowcoro_py PRIVATE
    flowcoro
    pybind11::module
    Python3::Module
    Threads::Threads
    atomic
)

target_compile_definitions(flowcoro_py PRIVATE
    FLOWCORO_PY_VERSION="${PROJECT_VERSION}"
)

set_target_properties(flowcoro_py PROPERTIES
    PREFIX ""
    OUTPUT_NAME "flowcoro_py"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
)

# 冒烟测试：把 build/python 放进 PYTHONPATH 后跑 pytest
if(FLOWCORO_BUILD_TESTS)
    add_test(NAME test_flowcoro_py
             COMMAND ${Python3_EXECUTABLE} -m pytest
                     "${CMAKE_CURRENT_SOURCE_DIR}/tests" -q)
    set_tests_properties(test_flowcoro_py PROPERTIES
        TIMEOUT 300
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python")
endif()

message(STATUS "Python bindings enabled -> ${CMAKE_BINARY_DIR}/python/flowcoro_py*.so")
