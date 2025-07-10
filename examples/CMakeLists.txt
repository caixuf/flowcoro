# FlowCoro Examples

# 基础示例
add_executable(basic_example example.cpp)
target_link_libraries(basic_example PRIVATE flowcoro)

# 增强示例
add_executable(enhanced_example enhanced_demo.cpp)
target_link_libraries(enhanced_example PRIVATE flowcoro)
