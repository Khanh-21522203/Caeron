add_executable(caeron_test_concurrent
    ring_buffer_test.cpp
    broadcast_test.cpp
    counters_manager_test.cpp
    mpsc_queue_test.cpp
    spsc_queue_test.cpp
)
target_link_libraries(caeron_test_concurrent PRIVATE caeron_concurrent caeron_platform_posix GTest::gtest_main)
add_test(NAME caeron_test_concurrent COMMAND caeron_test_concurrent)
set_tests_properties(caeron_test_concurrent PROPERTIES TIMEOUT 30)
