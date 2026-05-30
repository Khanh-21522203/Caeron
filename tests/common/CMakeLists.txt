add_executable(caeron_test_common
    byte_order_test.cpp
    bit_util_test.cpp
    unsafe_buffer_test.cpp
)
target_link_libraries(caeron_test_common PRIVATE caeron_common caeron_concurrent GTest::gtest_main)
add_test(NAME caeron_test_common COMMAND caeron_test_common)
