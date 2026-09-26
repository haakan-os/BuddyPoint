add_executable(SudokuTest SudokuTest.cpp ../../src/activities/apps/SudokuGame.cpp)
target_include_directories(SudokuTest PRIVATE ../../src)
target_link_libraries(SudokuTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SudokuTest)
