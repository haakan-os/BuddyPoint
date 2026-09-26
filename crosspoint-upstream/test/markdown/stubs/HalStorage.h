#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mdtest {
inline std::map<std::string, std::vector<uint8_t>> files;
inline std::set<std::string> directories;
inline std::string failWriteSuffix;
inline std::string failRenameTo;
inline bool failRead = false;
inline bool failClose = false;
inline size_t writes = 0;
inline void reset() {
  files.clear();
  directories.clear();
  failWriteSuffix.clear();
  failRenameTo.clear();
  failRead = false;
  failClose = false;
  writes = 0;
}
}  // namespace mdtest
class HalFile {
 public:
  void open(const std::string& name) {
    path = name;
    offset = 0;
    opened = true;
  }
  size_t size() const { return opened ? mdtest::files.at(path).size() : 0; }
  size_t position() const { return offset; }
  bool seek(size_t position) {
    if (!opened || position > size()) return false;
    offset = position;
    return true;
  }
  int read(void* data, size_t count) {
    if (!opened || mdtest::failRead) return -1;
    count = std::min(count, size() - offset);
    if (count) std::memcpy(data, mdtest::files.at(path).data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    ++mdtest::writes;
    if (!opened || (!mdtest::failWriteSuffix.empty() && path.ends_with(mdtest::failWriteSuffix))) return 0;
    auto& bytes = mdtest::files.at(path);
    bytes.resize(std::max(bytes.size(), offset + count));
    if (count) std::memcpy(bytes.data() + offset, data, count);
    offset += count;
    return count;
  }
  bool close() {
    opened = false;
    return !mdtest::failClose;
  }

 private:
  std::string path;
  size_t offset = 0;
  bool opened = false;
};
class HalStorage {
 public:
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    if (!mdtest::files.contains(path)) return false;
    file.open(path);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    mdtest::files[path].clear();
    file.open(path);
    return true;
  }
  bool exists(const char* path) { return mdtest::files.contains(path) || mdtest::directories.contains(path); }
  bool ensureDirectoryExists(const char* path) {
    mdtest::directories.insert(path);
    return true;
  }
  bool remove(const char* path) { return mdtest::files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (mdtest::failRenameTo == to || !mdtest::files.contains(from) || mdtest::files.contains(to)) return false;
    mdtest::files[to] = std::move(mdtest::files.at(from));
    mdtest::files.erase(from);
    return true;
  }
  bool removeDir(const char* path) {
    const std::string prefix = std::string(path) + "/";
    for (auto it = mdtest::files.begin(); it != mdtest::files.end();) {
      if (it->first.starts_with(prefix))
        it = mdtest::files.erase(it);
      else
        ++it;
    }
    mdtest::directories.erase(path);
    return true;
  }
};
inline HalStorage Storage;
inline void vTaskDelay(unsigned) {}
