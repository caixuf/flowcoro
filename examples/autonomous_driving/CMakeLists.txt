add_executable(ad_pipeline_demo ad_pipeline_demo.cpp)
target_link_libraries(ad_pipeline_demo PRIVATE flowcoro_net)
target_include_directories(ad_pipeline_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# RtExecutor control-loop companion: no DDS, measures tick jitter.
add_executable(rt_control_loop_demo rt_control_loop_demo.cpp)
target_link_libraries(rt_control_loop_demo PRIVATE flowcoro_net)
target_include_directories(rt_control_loop_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
