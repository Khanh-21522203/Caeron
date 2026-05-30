add_executable(caeron_test_platform
    mmap_test.cpp
    udp_socket_test.cpp
    epoll_poller_test.cpp
    thread_test.cpp
    clock_test.cpp
)
target_link_libraries(caeron_test_platform PRIVATE caeron_platform_posix GTest::gtest_main)
add_test(NAME caeron_test_platform COMMAND caeron_test_platform)
