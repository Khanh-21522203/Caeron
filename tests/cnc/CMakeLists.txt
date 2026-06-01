add_executable(caeron_test_cnc
    cnc_test.cpp
)
target_link_libraries(caeron_test_cnc PRIVATE caeron_concurrent caeron_platform_posix GTest::gtest_main)
add_test(NAME caeron_test_cnc COMMAND caeron_test_cnc)
set_tests_properties(caeron_test_cnc PROPERTIES TIMEOUT 30)
