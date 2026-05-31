add_executable(caeron_test_protocol
    header_flyweight_test.cpp
    data_header_flyweight_test.cpp
    setup_flyweight_test.cpp
    status_message_flyweight_test.cpp
    nak_flyweight_test.cpp
    rtt_measurement_flyweight_test.cpp
    error_flyweight_test.cpp
    response_setup_flyweight_test.cpp
    resolution_entry_flyweight_test.cpp
)
target_link_libraries(caeron_test_protocol PRIVATE caeron_protocol caeron_concurrent caeron_common GTest::gtest_main)
add_test(NAME caeron_test_protocol COMMAND caeron_test_protocol)
