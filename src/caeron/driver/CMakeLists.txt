# Driver - will be populated in later phases
add_library(caeron_driver STATIC
    media_driver.cpp
)
target_include_directories(caeron_driver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_driver PUBLIC caeron_common caeron_concurrent caeron_protocol caeron_command caeron_logbuffer caeron_cnc caeron_media)

add_subdirectory(flow_control)
add_subdirectory(congestion_control)
add_subdirectory(retransmit)
add_subdirectory(media)
add_subdirectory(status)
add_subdirectory(buffer)
