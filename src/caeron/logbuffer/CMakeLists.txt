add_library(caeron_logbuffer INTERFACE)
target_include_directories(caeron_logbuffer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_logbuffer INTERFACE caeron_common caeron_concurrent caeron_protocol)
