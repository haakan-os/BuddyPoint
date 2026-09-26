#pragma once
#include <HalStorage.h>

#include <cstdint>
#include <string>

namespace notes {
uint64_t hash(const char* value);
std::string sidecar(const std::string& path);
bool isFavourite(const std::string& path);
bool toggleFavourite(const std::string& path);

class Tools {
 public:
  static constexpr unsigned MAX_ITEMS = 64;
  // Fixed activity-owned labels; values are loaded on demand from SD.
  char labels[MAX_ITEMS][181]{};
  unsigned count = 0;
  bool load(const std::string& source, uint8_t kind);
  bool value(unsigned index, std::string& out);
  void close() { file.close(); }

 private:
  HalFile file;
  uint32_t offsets[MAX_ITEMS]{};
  uint16_t lengths[MAX_ITEMS]{};
};
}  // namespace notes
