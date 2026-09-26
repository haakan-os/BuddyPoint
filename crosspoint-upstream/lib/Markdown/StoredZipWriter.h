#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace markdown {
// Minimal stored ZIP32 writer for the five fixed EPUB members. No compression buffers.
class StoredZipWriter {
 public:
  using Write = bool (*)(void*, const uint8_t*, size_t);
  using Seek = bool (*)(void*, uint32_t);
  StoredZipWriter(Write write, Seek seek, void* context) : write(write), seek(seek), context(context) {}
  bool begin(std::string_view name);
  bool append(const void* data, size_t size);
  bool end();
  bool finish();

 private:
  struct Entry {
    char name[32]{};
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t crc = 0;
    uint16_t nameLength = 0;
  } entries[5];
  unsigned count = 0;
  uint32_t position = 0;
  bool active = false;
  bool ok = true;
  Write write;
  Seek seek;
  void* context;
  bool raw(const void* bytes, size_t size);
};
}  // namespace markdown
