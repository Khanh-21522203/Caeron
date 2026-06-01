add_library(caeron_cnc STATIC
    mapped_log_buffers_factory.cpp
)
target_include_directories(caeron_cnc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_cnc PUBLIC caeron_common caeron_concurrent caeron_logbuffer caeron_platform_posix)
