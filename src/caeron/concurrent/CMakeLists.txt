add_library(caeron_concurrent INTERFACE)
target_include_directories(caeron_concurrent INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_concurrent INTERFACE caeron_common)
