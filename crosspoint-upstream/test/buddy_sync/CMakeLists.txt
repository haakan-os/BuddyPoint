add_executable(BuddySyncStateTest BuddySyncStateTest.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp)
target_include_directories(BuddySyncStateTest PRIVATE ../../src ${REPO_ROOT}/lib/Utf8)
target_link_libraries(BuddySyncStateTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BuddySyncStateTest)
