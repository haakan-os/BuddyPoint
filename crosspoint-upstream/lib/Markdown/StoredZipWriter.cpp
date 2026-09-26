#include "StoredZipWriter.h"

#include <cstring>
#include <limits>

namespace markdown {
namespace {
void put16(uint8_t* p, uint16_t n) {
  p[0] = n;
  p[1] = n >> 8;
}
void put32(uint8_t* p, uint32_t n) {
  put16(p, n);
  put16(p + 2, n >> 16);
}
}  // namespace
bool StoredZipWriter::raw(const void* bytes, size_t size) {
  if (!ok || size > std::numeric_limits<uint32_t>::max() - position) {
    ok = false;
    return false;
  }
  ok = write(context, static_cast<const uint8_t*>(bytes), size);
  if (ok) position += size;
  return ok;
}
bool StoredZipWriter::begin(std::string_view name) {
  if (!ok || active || count >= 5 || name.empty() || name.size() >= sizeof(entries[0].name)) {
    ok = false;
    return false;
  }
  auto& entry = entries[count];
  std::memcpy(entry.name, name.data(), name.size());
  entry.nameLength = name.size();
  entry.offset = position;
  entry.crc = 0xffffffffu;
  uint8_t header[30]{};
  put32(header, 0x04034b50);
  put16(header + 4, 20);
  put16(header + 12, 33);
  put16(header + 26, entry.nameLength);
  raw(header, sizeof(header));
  raw(name.data(), name.size());
  active = ok;
  return ok;
}
bool StoredZipWriter::append(const void* data, size_t size) {
  if (!active || !ok) {
    ok = false;
    return false;
  }
  auto& entry = entries[count];
  if (size > std::numeric_limits<uint32_t>::max() - entry.size) {
    ok = false;
    return false;
  }
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    entry.crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit) entry.crc = (entry.crc >> 1) ^ (0xedb88320u & (0u - (entry.crc & 1)));
  }
  entry.size += size;
  return raw(data, size);
}
bool StoredZipWriter::end() {
  if (!active || !ok) {
    ok = false;
    return false;
  }
  auto& entry = entries[count];
  entry.crc ^= 0xffffffffu;
  uint8_t sizes[12];
  put32(sizes, entry.crc);
  put32(sizes + 4, entry.size);
  put32(sizes + 8, entry.size);
  ok = seek(context, entry.offset + 14) && write(context, sizes, sizeof(sizes)) && seek(context, position);
  active = false;
  ++count;
  return ok;
}
bool StoredZipWriter::finish() {
  if (!ok || active || !count) {
    ok = false;
    return false;
  }
  const uint32_t start = position;
  for (unsigned i = 0; i < count && ok; ++i) {
    const auto& entry = entries[i];
    uint8_t header[46]{};
    put32(header, 0x02014b50);
    put16(header + 4, 20);
    put16(header + 6, 20);
    put16(header + 14, 33);
    put32(header + 16, entry.crc);
    put32(header + 20, entry.size);
    put32(header + 24, entry.size);
    put16(header + 28, entry.nameLength);
    put32(header + 42, entry.offset);
    raw(header, sizeof(header));
    raw(entry.name, entry.nameLength);
  }
  const uint32_t directorySize = position - start;
  uint8_t end[22]{};
  put32(end, 0x06054b50);
  put16(end + 8, count);
  put16(end + 10, count);
  put32(end + 12, directorySize);
  put32(end + 16, start);
  return raw(end, sizeof(end));
}
}  // namespace markdown
