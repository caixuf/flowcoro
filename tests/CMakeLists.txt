# FlowCoro Tests

# 基本测试
add_executable(flowcoro_unit_tests coro_tests.cpp)
target_link_libraries(flowcoro_unit_tests PRIVATE flowcoro)

# 简单测试
add_executable(flowcoro_simple_tests test_simple.cpp)
target_link_libraries(flowcoro_simple_tests PRIVATE flowcoro)

# 添加到测试套件
add_test(NAME UnitTests COMMAND flowcoro_unit_tests)
add_test(NAME SimpleTests COMMAND flowcoro_simple_tests)

# 设置测试属性
set_tests_properties(UnitTests PROPERTIES
    TIMEOUT 30
    LABELS "unit"
)

set_tests_properties(SimpleTests PROPERTIES
    TIMEOUT 10
    LABELS "simple"
)
