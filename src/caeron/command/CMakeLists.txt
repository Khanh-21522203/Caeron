add_library(caeron_command STATIC
    correlated_message_flyweight.cpp
    publication_message_flyweight.cpp
    subscription_message_flyweight.cpp
    remove_message_flyweight.cpp
    counter_message_flyweight.cpp
    counter_update_flyweight.cpp
    image_message_flyweight.cpp
    publication_buffers_ready_flyweight.cpp
    image_buffers_ready_flyweight.cpp
    subscription_ready_flyweight.cpp
    error_response_flyweight.cpp
    operation_succeeded_flyweight.cpp
    client_timeout_flyweight.cpp
    terminate_driver_flyweight.cpp
    remove_publication_flyweight.cpp
    remove_subscription_flyweight.cpp
    publication_error_frame_flyweight.cpp
)
target_include_directories(caeron_command PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_command PUBLIC caeron_common caeron_concurrent)
