add_library(caeron_protocol INTERFACE)
target_include_directories(caeron_protocol INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_protocol INTERFACE caeron_common caeron_concurrent)
