# FlowCoro Tests - 精简版本

# 综合测试 (包含所有功能：无锁数据结构、协程、异步编程、网络请求等)
add_executable(flowcoro_tests test_simple.cpp)
target_link_libraries(flowcoro_tests PRIVATE flowcoro flowcoro_net)

# 添加到测试套件
add_test(NAME ComprehensiveTests COMMAND flowcoro_tests)

# 设置测试属性
set_tests_properties(ComprehensiveTests PROPERTIES
    TIMEOUT 60
    LABELS "comprehensive;async;lockfree;coroutine;network"
)
