add_executable(Ao3Test Ao3Test.cpp ../../lib/Ao3/Ao3.cpp ../../lib/Ao3/Ao3Badge.cpp)
target_include_directories(Ao3Test PRIVATE ../../lib/Ao3)
target_link_libraries(Ao3Test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Ao3Test)

FetchContent_Declare(ao3_arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR src/ArduinoJson
)
FetchContent_MakeAvailable(ao3_arduinojson)
find_package(ZLIB REQUIRED)
add_executable(Ao3ClientTest
  Ao3ClientTest.cpp
  Ao3WebTest.cpp
  ../../src/network/Ao3WebJobs.cpp
  ../../src/network/Ao3Client.cpp
  ../../lib/Ao3/Ao3.cpp
  ../../lib/ZipFile/ZipFile.cpp
  ../../lib/miniz/src/InflateStream.cpp
  ../../lib/miniz/src/miniz_impl.c
  ../../lib/Memory/BuildScratch.cpp
  ../../lib/hal/HalMemory.cpp
)
target_include_directories(Ao3ClientTest PRIVATE
  stubs ../inflate_stream/stubs
  ../../lib/Ao3 ../../src/network ../../lib/AtomicFile ../../lib/Memory
  ../../lib/ZipFile ../../lib/miniz/src ../../lib/hal
  ${ao3_arduinojson_SOURCE_DIR}/src
)
target_compile_definitions(Ao3ClientTest PRIVATE CROSSPOINT_VERSION="test")
target_link_libraries(Ao3ClientTest PRIVATE crosspoint_test_common GTest::gtest_main ZLIB::ZLIB)
gtest_discover_tests(Ao3ClientTest)
