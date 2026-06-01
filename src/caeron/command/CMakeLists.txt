add_library(caeron_command INTERFACE)
target_include_directories(caeron_command INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_command INTERFACE caeron_common caeron_concurrent)
