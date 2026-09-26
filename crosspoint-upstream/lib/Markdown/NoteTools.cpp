#include "NoteTools.h"

#include <AtomicFile.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace notes {
namespace {
constexpr uint64_t BASIS = 14695981039346656037ULL;
uint64_t add(uint64_t hash, uint8_t byte) { return (hash ^ byte) * 1099511628211ULL; }
uint64_t little(const uint8_t* data, unsigned size) {
  uint64_t value = 0;
  for (unsigned i = 0; i < size; ++i) value |= uint64_t(data[i]) << (8 * i);
  return value;
}
bool fingerprint(HalFile& file, size_t size, uint64_t& result) {
  uint8_t buffer[128];
  result = BASIS;
  while (size) {
    const size_t amount = size < sizeof(buffer) ? size : sizeof(buffer);
    if (file.read(buffer, amount) != static_cast<int>(amount)) return false;
    for (size_t i = 0; i < amount; ++i) result = add(result, buffer[i]);
    size -= amount;
    if (size % 4096 < sizeof(buffer)) vTaskDelay(1);
  }
  return true;
}
std::string favouritePath(const std::string& path) {
  char name[64];
  snprintf(name, sizeof(name), "/.crosspoint/favourites/%016llx.fav",
           static_cast<unsigned long long>(hash(path.c_str())));
  return name;
}
}  // namespace
uint64_t hash(const char* value) {
  uint64_t result = BASIS;
  while (*value) result = add(result, static_cast<uint8_t>(*value++));
  return result;
}
std::string sidecar(const std::string& path) {
  const auto slash = path.find_last_of('/');
  const auto base = slash == std::string::npos ? 0 : slash + 1;
  char name[48];
  snprintf(name, sizeof(name), "buddy-notes-%016llx.bnotes",
           static_cast<unsigned long long>(hash(path.c_str() + base)));
  return path.substr(0, base) + name;
}
bool isFavourite(const std::string& path) {
  const auto target = favouritePath(path);
  if (!atomic_file::recover(target) || !Storage.exists(target.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForRead("FAV", target.c_str(), file) || file.size() != path.size()) return false;
  char buffer[128];
  size_t position = 0;
  while (position < path.size()) {
    const size_t amount = std::min(sizeof(buffer), path.size() - position);
    if (file.read(reinterpret_cast<uint8_t*>(buffer), amount) != static_cast<int>(amount) ||
        memcmp(buffer, path.data() + position, amount) != 0)
      return false;
    position += amount;
  }
  return true;
}
bool toggleFavourite(const std::string& path) {
  const auto target = favouritePath(path);
  if (!atomic_file::recover(target)) return false;
  if (isFavourite(path)) return Storage.remove(target.c_str());
  if (!Storage.ensureDirectoryExists("/.crosspoint/favourites")) return false;
  const auto temporary = target + ".tmp";
  {
    HalFile file;
    if (!Storage.openFileForWrite("FAV", temporary.c_str(), file) ||
        file.write(reinterpret_cast<const uint8_t*>(path.data()), path.size()) != path.size())
      return false;
    if (!file.close()) return false;
  }
  return atomic_file::publish(target, temporary);
}
bool Tools::load(const std::string& source, uint8_t kind) {
  count = 0;
  file.close();
  HalFile original;
  if (!Storage.openFileForRead("NOTES", source.c_str(), original)) return false;
  const auto target = sidecar(source);
  if (!Storage.openFileForRead("NOTES", target.c_str(), file)) return false;
  uint8_t header[32];
  uint64_t actual;
  if (file.read(header, sizeof(header)) != sizeof(header) || memcmp(header, "BUDNOTE1", 8) ||
      little(header + 8, 4) != original.size() || original.size() > 8 * 1024 * 1024 ||
      !fingerprint(original, original.size(), actual) || actual != little(header + 12, 8))
    return false;
  const auto size = little(header + 20, 4);
  if (size < 2 || size > 150000 || file.size() != size + 32 || !fingerprint(file, size, actual) ||
      actual != little(header + 24, 8) || !file.seek(32))
    return false;
  uint8_t record[5];
  if (file.read(record, 2) != 2) return false;
  const unsigned total = little(record, 2);
  if (total > MAX_ITEMS) return false;
  uint32_t position = 34;
  for (unsigned i = 0; i < total; ++i) {
    if (file.read(record, 5) != 5) {
      count = 0;
      return false;
    }
    position += 5;
    const unsigned label = little(record + 1, 2), length = little(record + 3, 2);
    if ((record[0] != 1 && record[0] != 2) || label == 0 || label > 180 || length == 0 ||
        length > (record[0] == 1 ? 768u : 2048u) || position + label + length > file.size()) {
      count = 0;
      return false;
    }
    if (record[0] == kind) {
      if (file.read(reinterpret_cast<uint8_t*>(labels[count]), label) != static_cast<int>(label)) {
        count = 0;
        return false;
      }
      labels[count][label] = 0;
      offsets[count] = position + label;
      lengths[count++] = length;
    }
    position += label + length;
    if (!file.seek(position)) {
      count = 0;
      return false;
    }
  }
  if (position != file.size()) {
    count = 0;
    return false;
  }
  return true;
}
bool Tools::value(unsigned index, std::string& out) {
  if (index >= count || !file.seek(offsets[index])) return false;
  // One bounded value per selection, never per render or input poll.
  out.resize(lengths[index]);
  return file.read(reinterpret_cast<uint8_t*>(out.data()), out.size()) == static_cast<int>(out.size()) &&
         out.find('\0') == std::string::npos;
}
}  // namespace notes
