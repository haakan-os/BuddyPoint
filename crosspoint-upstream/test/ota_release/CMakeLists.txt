add_executable(OtaReleaseTest
  OtaReleaseTest.cpp
  ${REPO_ROOT}/lib/JsonParser/ReleaseJsonParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)
target_include_directories(OtaReleaseTest PRIVATE ${REPO_ROOT}/lib/JsonParser)
target_link_libraries(OtaReleaseTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(OtaReleaseTest)
