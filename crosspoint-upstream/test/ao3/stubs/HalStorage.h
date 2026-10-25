#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ao3test {
inline std::map<std::string, std::vector<uint8_t>> files;
inline std::set<std::string> directories;
inline std::map<std::string, std::string> aliases;
inline std::string failWriteSuffix;
inline std::string failRenameTo;
inline bool failRead = false;
inline bool failClose = false;
inline size_t writes = 0;
inline void reset() {
  files.clear();
  aliases.clear();
  directories.clear();
  failWriteSuffix.clear();
  failRenameTo.clear();
  failRead = false;
  failClose = false;
  writes = 0;
}
}  // namespace ao3test
class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(const uint8_t*, size_t) = 0;
};
class HalFile {
 public:
  void initialize() {
    initialized = true;
    opened = false;
  }
  void open(const std::string& name) {
    initialize();
    path = name;
    offset = 0;
    opened = true;
    directory = ao3test::directories.contains(path);
    children.clear();
    if (directory) {
      auto add = [&](const std::string& child) {
        const auto relative = child.substr(path.size() + 1);
        if (child.starts_with(path + "/") && relative.find('/') == std::string::npos) children.push_back(child);
      };
      for (const auto& [child, data] : ao3test::files)
        if (child.size() > path.size()) add(child);
      for (const auto& child : ao3test::directories)
        if (child.size() > path.size()) add(child);
    }
  }
  explicit operator bool() const { return opened; }
  bool isDirectory() const { return directory; }
  HalFile openNextFile() {
    HalFile file;
    if (opened && offset < children.size()) file.open(children[offset++]);
    return file;
  }
  void getName(char* out, size_t count) const {
    const auto name = path.substr(path.rfind('/') + 1);
    if (count) {
      std::strncpy(out, name.c_str(), count - 1);
      out[count - 1] = 0;
    }
  }
  int available() const { return opened && !directory && offset < size(); }
  int read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
  }
  bool seekCur(int64_t distance) { return seek(offset + distance); }
  void flush() {}
  size_t size() const { return opened ? ao3test::files.at(path).size() : 0; }
  size_t position() const { return offset; }
  bool seek(size_t position) {
    if (!opened || position > size()) return false;
    offset = position;
    return true;
  }
  int read(void* data, size_t count) {
    if (!opened || ao3test::failRead) return -1;
    count = std::min(count, size() - offset);
    if (count) std::memcpy(data, ao3test::files.at(path).data() + offset, count);
    offset += count;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t count) {
    ++ao3test::writes;
    if (!opened || (!ao3test::failWriteSuffix.empty() && path.ends_with(ao3test::failWriteSuffix))) return 0;
    auto& bytes = ao3test::files.at(path);
    bytes.resize(std::max(bytes.size(), offset + count));
    if (count) std::memcpy(bytes.data() + offset, data, count);
    offset += count;
    return count;
  }
  bool close() {
    // Match the firmware HAL even when host tests compile with NDEBUG.
    if (!initialized) std::abort();
    opened = false;
    return !ao3test::failClose;
  }

 private:
  std::string path;
  size_t offset = 0;
  bool initialized = false;
  bool opened = false;
  bool directory = false;
  std::vector<std::string> children;
};
class HalStorage {
 public:
  HalFile open(const char* path) {
    HalFile file;
    file.initialize();
    if (ao3test::aliases.contains(path))
      file.open(ao3test::aliases.at(path));
    else if (exists(path))
      file.open(path);
    return file;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& file) {
    file.initialize();
    if (!ao3test::files.contains(path)) return false;
    file.open(path);
    return true;
  }
  bool openFileForWrite(const char*, const std::string& path, HalFile& file) {
    ao3test::files[path].clear();
    file.open(path);
    return true;
  }
  bool exists(const char* path) { return ao3test::files.contains(path) || ao3test::directories.contains(path); }
  bool ensureDirectoryExists(const char* path) {
    ao3test::directories.insert(path);
    return true;
  }
  bool remove(const char* path) { return ao3test::files.erase(path) != 0; }
  bool rename(const char* from, const char* to) {
    if (ao3test::failRenameTo == to) {
      ao3test::failRenameTo.clear();
      return false;
    }
    if (!ao3test::files.contains(from) || ao3test::files.contains(to)) return false;
    ao3test::files[to] = std::move(ao3test::files.at(from));
    ao3test::files.erase(from);
    return true;
  }
  bool removeDir(const char* path) {
    const std::string prefix = std::string(path) + "/";
    for (auto it = ao3test::files.begin(); it != ao3test::files.end();) {
      if (it->first.starts_with(prefix))
        it = ao3test::files.erase(it);
      else
        ++it;
    }
    ao3test::directories.erase(path);
    return true;
  }
};
inline HalStorage Storage;
inline void vTaskDelay(unsigned) {}
