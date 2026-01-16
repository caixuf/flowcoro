# FlowCoro 
cmake_minimum_required(VERSION 3.16)

#  C++ 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines -pthread -g0 -O0 -fno-inline")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-depth=128 -ftemplate-backtrace-limit=10")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-stack-protector --param ggc-min-expand=10 --param ggc-min-heapsize=32768")

# 
include_directories(${CMAKE_SOURCE_DIR}/include)

# 
find_package(Threads REQUIRED)

#  - 
add_executable(test_core test_core.cpp)
target_link_libraries(test_core flowcoro_net Threads::Threads)

#  - Channel
add_executable(test_new_features test_new_features.cpp)
target_link_libraries(test_new_features flowcoro_net Threads::Threads)

# 
add_executable(test_network test_network.cpp)

# 
add_executable(test_database test_database.cpp)

# RPC
add_executable(test_rpc test_rpc.cpp)

# 
add_executable(test_cross_thread test_cross_thread.cpp)
target_link_libraries(test_cross_thread flowcoro_net Threads::Threads)

# when_any
add_executable(test_when_any test_when_any.cpp)

# sleep_for
add_executable(test_sleep_for test_sleep_for.cpp)

# 
add_executable(test_network_professional test_network_professional.cpp)

# 
target_link_libraries(test_core
    flowcoro_net
    Threads::Threads
)

# 
target_link_libraries(test_network
    flowcoro_net
    Threads::Threads
)

# 
target_link_libraries(test_database
    flowcoro_net
    Threads::Threads
)

# RPC
target_link_libraries(test_rpc
    flowcoro_net
    Threads::Threads
)

# 
target_link_libraries(test_cross_thread
    flowcoro_net
    Threads::Threads
)

# when_any
target_link_libraries(test_when_any
    flowcoro_net
    Threads::Threads
)

# sleep_for
target_link_libraries(test_sleep_for
    flowcoro_net
    Threads::Threads
)

# 
target_link_libraries(test_network_professional
    flowcoro_net
    Threads::Threads
)

# 
add_test(NAME test_core COMMAND test_core)
add_test(NAME test_new_features COMMAND test_new_features)
add_test(NAME test_network COMMAND test_network)
add_test(NAME test_database COMMAND test_database)
add_test(NAME test_rpc COMMAND test_rpc)
add_test(NAME test_cross_thread COMMAND test_cross_thread)
add_test(NAME test_when_any COMMAND test_when_any)
add_test(NAME test_sleep_for COMMAND test_sleep_for)
add_test(NAME test_network_professional COMMAND test_network_professional)

# 
set_tests_properties(test_core PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_new_features PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_network PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_database PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_rpc PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_cross_thread PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_when_any PROPERTIES
    TIMEOUT 60
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_sleep_for PROPERTIES
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

set_tests_properties(test_network_professional PROPERTIES
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 
set_target_properties(test_core test_new_features test_network test_database test_rpc test_cross_thread test_when_any test_sleep_for test_network_professional PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests
)

message(STATUS "FlowCoro core test configured successfully")
message(STATUS "Test executable will be built in: ${CMAKE_BINARY_DIR}/tests")
