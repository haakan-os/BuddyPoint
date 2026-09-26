#include <Memory.h>
#include <ZipFile.h>
#include <gtest/gtest.h>
#include <zlib.h>

namespace {
void put16(std::vector<uint8_t>& bytes, uint16_t value) {
  bytes.push_back(value & 255);
  bytes.push_back(value >> 8);
}
void put32(std::vector<uint8_t>& bytes, uint32_t value) {
  put16(bytes, value & 65535);
  put16(bytes, value >> 16);
}
void set16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
  bytes[at] = value & 255;
  bytes[at + 1] = value >> 8;
}
void set32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
  set16(bytes, at, value & 65535);
  set16(bytes, at + 2, value >> 16);
}
std::vector<uint8_t> rawDeflate(const std::vector<uint8_t>& data) {
  z_stream stream{};
  EXPECT_EQ(deflateInit2(&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY), Z_OK);
  std::vector<uint8_t> result(deflateBound(&stream, data.size()));
  stream.next_in = const_cast<uint8_t*>(data.data());
  stream.avail_in = data.size();
  stream.next_out = result.data();
  stream.avail_out = result.size();
  EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
  result.resize(stream.total_out);
  deflateEnd(&stream);
  return result;
}
class Sink : public Print {
 public:
  std::vector<uint8_t> bytes;
  size_t limit = 200000;
  bool invalidLength = false;
  size_t write(const uint8_t* data, size_t size) override {
    if (size > 200000) {
      invalidLength = true;
      return 0;
    }
    size = std::min(size, limit - bytes.size());
    bytes.insert(bytes.end(), data, data + size);
    return size;
  }
};
class ZipFileTest : public ::testing::Test {
 protected:
  const std::string path = "book.epub";
  const std::string entry = "image.png";
  std::vector<uint8_t> payload;
  size_t dataOffset = 0;
  size_t eocdOffset = 0;
  void SetUp() override {
    ziptest::readErrorAt = ziptest::shortReadAt = ziptest::failedSeekAt = std::numeric_limits<size_t>::max();
    ziptest::maxBuffer = std::numeric_limits<size_t>::max();
    ziptest::failSecondLargeBuffer = false;
    ziptest::allocationSizes.clear();
    payload.resize(100003);
    uint32_t seed = 17;
    for (auto& byte : payload) {
      seed = seed * 1664525 + 1013904223;
      byte = seed >> 24;
    }
  }
  void archive(bool compressed, size_t commentSize = 0) {
    auto data = compressed ? rawDeflate(payload) : payload;
    auto& bytes = ziptest::file;
    bytes.clear();
    const uint16_t method = compressed ? 8 : 0;
    const uint32_t crc = crc32(0, payload.data(), payload.size());
    put32(bytes, 0x04034b50);
    put16(bytes, 20);
    put16(bytes, 0);
    put16(bytes, method);
    put32(bytes, 0);
    put32(bytes, crc);
    put32(bytes, data.size());
    put32(bytes, payload.size());
    put16(bytes, entry.size());
    put16(bytes, 0);
    bytes.insert(bytes.end(), entry.begin(), entry.end());
    dataOffset = bytes.size();
    bytes.insert(bytes.end(), data.begin(), data.end());
    const size_t cd = bytes.size();
    put32(bytes, 0x02014b50);
    put16(bytes, 20);
    put16(bytes, 20);
    put16(bytes, 0);
    put16(bytes, method);
    put32(bytes, 0);
    put32(bytes, crc);
    put32(bytes, data.size());
    put32(bytes, payload.size());
    put16(bytes, entry.size());
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, 0);
    put32(bytes, 0);
    put32(bytes, 0);
    bytes.insert(bytes.end(), entry.begin(), entry.end());
    eocdOffset = bytes.size();
    put32(bytes, 0x06054b50);
    put16(bytes, 0);
    put16(bytes, 0);
    put16(bytes, 1);
    put16(bytes, 1);
    put32(bytes, eocdOffset - cd);
    put32(bytes, cd);
    put16(bytes, commentSize);
    bytes.insert(bytes.end(), commentSize, 'c');
  }
  bool extract(Sink& out, size_t chunk = 8192, bool early = false) {
    ZipFile zip(path);
    return zip.readFileToStream(entry.c_str(), out, chunk, early);
  }
};
TEST_F(ZipFileTest, StoredAndDeflatedRoundTripAcrossWindows) {
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    ASSERT_TRUE(extract(out));
    EXPECT_EQ(out.bytes, payload);
  }
}
TEST_F(ZipFileTest, ReadsAllLegalCommentLengthsAndSplitRecords) {
  payload.resize(13);
  for (size_t length : {0, 1, 105, 106, 107, 108, 127, 128, 1024, 4096, 65535}) {
    SCOPED_TRACE(length);
    archive(false, length);
    Sink out;
    ASSERT_TRUE(extract(out));
    EXPECT_EQ(out.bytes, payload);
  }
}
TEST_F(ZipFileTest, IgnoresFalseDirectorySignatureInsideComment) {
  archive(false, 512);
  set32(ziptest::file, ziptest::file.size() - 32, 0x06054b50);
  Sink out;
  EXPECT_TRUE(extract(out));
  EXPECT_EQ(out.bytes, payload);
}
TEST_F(ZipFileTest, RejectsInvalidDirectoryRangesAndUnsupportedArchives) {
  for (int damage = 0; damage < 5; ++damage) {
    archive(false);
    if (damage == 0) set32(ziptest::file, eocdOffset + 16, 0xffffff00);
    if (damage == 1) set32(ziptest::file, eocdOffset + 12, 0xffffffff);
    if (damage == 2) set16(ziptest::file, eocdOffset + 4, 1);
    if (damage == 3) set16(ziptest::file, eocdOffset + 20, 12);
    if (damage == 4) ziptest::file.resize(21);
    Sink out;
    EXPECT_FALSE(extract(out));
    EXPECT_TRUE(out.bytes.empty());
  }
}
TEST_F(ZipFileTest, DirectoryReadAndSeekFailuresStopCleanly) {
  for (int fault = 0; fault < 3; ++fault) {
    archive(false);
    const size_t at = ziptest::file.size() - 128;
    if (fault == 0) ziptest::readErrorAt = at;
    if (fault == 1) ziptest::shortReadAt = at;
    if (fault == 2) ziptest::failedSeekAt = at;
    Sink out;
    EXPECT_FALSE(extract(out));
    EXPECT_TRUE(out.bytes.empty());
    ziptest::readErrorAt = ziptest::shortReadAt = ziptest::failedSeekAt = std::numeric_limits<size_t>::max();
  }
}
TEST_F(ZipFileTest, NegativeDataReadNeverReachesSinkEvenWithEarlyStop) {
  for (bool compressed : {false, true}) {
    archive(compressed);
    ziptest::readErrorAt = dataOffset;
    Sink out;
    EXPECT_FALSE(extract(out, 8192, true));
    EXPECT_FALSE(out.invalidLength);
    EXPECT_TRUE(out.bytes.empty());
  }
}
TEST_F(ZipFileTest, ZeroChunkSizeIsRejected) {
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    EXPECT_FALSE(extract(out, 0));
    EXPECT_TRUE(out.bytes.empty());
  }
}
TEST_F(ZipFileTest, FallsBackToSmallBuffersUnderMemoryPressure) {
  ziptest::maxBuffer = 512;
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    ASSERT_TRUE(extract(out));
    EXPECT_EQ(out.bytes, payload);
  }
}
TEST_F(ZipFileTest, RetriesWhenSecondBufferAllocationFails) {
  archive(true);
  ziptest::failSecondLargeBuffer = true;
  Sink out;
  ASSERT_TRUE(extract(out));
  EXPECT_EQ(out.bytes, payload);
}
TEST_F(ZipFileTest, UnrecoverableBufferFailureStopsBeforeWriting) {
  ziptest::maxBuffer = 0;
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    EXPECT_FALSE(extract(out));
    EXPECT_TRUE(out.bytes.empty());
  }
}
TEST_F(ZipFileTest, ShortWritesFailExceptForExplicitHeaderProbes) {
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    out.limit = 24;
    EXPECT_FALSE(extract(out));
    Sink probe;
    probe.limit = 24;
    EXPECT_TRUE(extract(probe, 1024, true));
    EXPECT_EQ(probe.bytes, std::vector<uint8_t>(payload.begin(), payload.begin() + 24));
  }
}
TEST_F(ZipFileTest, CorruptDeflateIsRejected) {
  archive(true);
  ziptest::file[dataOffset] = 0xff;
  Sink out;
  EXPECT_FALSE(extract(out));
}
TEST_F(ZipFileTest, SmallCallerChunksAndEmptyEntriesWork) {
  payload.clear();
  for (bool compressed : {false, true}) {
    archive(compressed);
    Sink out;
    EXPECT_TRUE(extract(out, 1));
    EXPECT_TRUE(out.bytes.empty());
  }
}
}  // namespace
