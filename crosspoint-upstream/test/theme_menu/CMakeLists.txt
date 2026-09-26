add_executable(ThemeMenuLayoutTest ThemeMenuLayoutTest.cpp)
target_include_directories(ThemeMenuLayoutTest PRIVATE ../../src)
target_link_libraries(ThemeMenuLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ThemeMenuLayoutTest)
