add_library(caeron_platform_posix STATIC
    mmap.cpp
    udp_socket.cpp
    epoll_poller.cpp
    thread.cpp
    clock.cpp
)
target_include_directories(caeron_platform_posix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_link_libraries(caeron_platform_posix PUBLIC caeron_common)
