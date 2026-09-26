#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace ziptest {
inline std::vector<uint8_t> file;
inline size_t readErrorAt = std::numeric_limits<size_t>::max();
inline size_t shortReadAt = std::numeric_limits<size_t>::max();
inline size_t failedSeekAt = std::numeric_limits<size_t>::max();
}  // namespace ziptest
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
class HalFile {
 public:
  void open() {
    open_ = true;
    pos_ = 0;
  }
  bool close() {
    open_ = false;
    return true;
  }
  explicit operator bool() const { return open_; }
  size_t size() const { return ziptest::file.size(); }
  size_t position() const { return pos_; }
  int available() const { return open_ && pos_ < size(); }
  bool seek(size_t pos) {
    if (!open_ || pos > size() || pos == ziptest::failedSeekAt) return false;
    pos_ = pos;
    return true;
  }
  bool seekCur(int64_t offset) { return seek(pos_ + offset); }
  int read(void* dest, size_t count) {
    if (pos_ == ziptest::readErrorAt) return -1;
    if (!open_ || pos_ > size()) return 0;
    count = std::min(count, size() - pos_);
    if (pos_ == ziptest::shortReadAt && count > 0) --count;
    std::memcpy(dest, ziptest::file.data() + pos_, count);
    pos_ += count;
    return static_cast<int>(count);
  }

 private:
  bool open_ = false;
  size_t pos_ = 0;
};
class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    file.open();
    return true;
  }
};
inline HalStorage Storage;
