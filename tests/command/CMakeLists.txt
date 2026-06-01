add_executable(caeron_test_command
    command_flyweight_test.cpp
)
target_link_libraries(caeron_test_command PRIVATE caeron_command caeron_concurrent GTest::gtest_main)
add_test(NAME caeron_test_command COMMAND caeron_test_command)
set_tests_properties(caeron_test_command PROPERTIES TIMEOUT 30)
