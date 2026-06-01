add_executable(caeron_test_media
    socket_address_parser_test.cpp
    network_util_test.cpp
    interface_search_address_test.cpp
    named_interface_test.cpp
    control_mode_test.cpp
    udp_channel_test.cpp
    wildcard_port_manager_test.cpp
    udp_channel_transport_test.cpp
    send_channel_endpoint_test.cpp
    receive_channel_endpoint_test.cpp
    image_connection_test.cpp
    data_transport_poller_test.cpp
    control_transport_poller_test.cpp
)
target_link_libraries(caeron_test_media PRIVATE caeron_media GTest::gtest_main)
add_test(NAME caeron_test_media COMMAND caeron_test_media)
