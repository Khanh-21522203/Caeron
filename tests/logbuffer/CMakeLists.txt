add_executable(caeron_test_logbuffer
    buffer_claim_test.cpp
    term_rebuilder_test.cpp
    term_reader_test.cpp
    term_scanner_test.cpp
    term_gap_scanner_test.cpp
    term_block_scanner_test.cpp
    term_gap_filler_test.cpp
    term_unblocker_test.cpp
    log_buffer_unblocker_test.cpp
)
target_link_libraries(caeron_test_logbuffer PRIVATE caeron_logbuffer GTest::gtest_main)
target_include_directories(caeron_test_logbuffer PRIVATE ${CMAKE_SOURCE_DIR}/src/caeron)

add_test(NAME caeron_test_logbuffer COMMAND caeron_test_logbuffer)
set_tests_properties(caeron_test_logbuffer PROPERTIES TIMEOUT 30)
