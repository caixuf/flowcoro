add_executable(ad_pipeline_demo ad_pipeline_demo.cpp)
target_link_libraries(ad_pipeline_demo PRIVATE flowcoro_net)
target_include_directories(ad_pipeline_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
