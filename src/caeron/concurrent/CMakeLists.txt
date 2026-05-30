add_library(caeron_concurrent STATIC
    unsafe_buffer.cpp
)
target_include_directories(caeron_concurrent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_concurrent PUBLIC caeron_common)
