add_library(caeron_protocol STATIC
    header_flyweight.cpp
    data_header_flyweight.cpp
    setup_flyweight.cpp
    status_message_flyweight.cpp
    nak_flyweight.cpp
    rtt_measurement_flyweight.cpp
    error_flyweight.cpp
    response_setup_flyweight.cpp
)
target_include_directories(caeron_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_protocol PUBLIC caeron_common caeron_concurrent)
