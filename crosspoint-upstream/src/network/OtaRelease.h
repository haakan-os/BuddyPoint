#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace ota_release {

constexpr char latestReleaseUrl[] = "https://api.github.com/repos/haakan-os/BuddyPoint/releases/latest";

inline const char* withoutTagPrefix(const char* version) { return version[0] == 'v' ? version + 1 : version; }

inline bool parseVersion(const char* version, uint32_t (&parts)[3], const char*& suffix) {
  const char* cursor = withoutTagPrefix(version);
  for (size_t i = 0; i < 3; ++i) {
    if (*cursor < '0' || *cursor > '9') return false;
    uint32_t value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      const uint32_t digit = *cursor++ - '0';
      if (value > (UINT32_MAX - digit) / 10) return false;
      value = value * 10 + digit;
    }
    parts[i] = value;
    if (i < 2 && *cursor++ != '.') return false;
  }
  suffix = cursor;
  return *cursor == '\0' || *cursor == '-' || *cursor == '+';
}

inline bool isNewer(const char* current, const char* latest) {
  uint32_t currentParts[3] = {};
  uint32_t latestParts[3] = {};
  const char* currentSuffix = nullptr;
  const char* latestSuffix = nullptr;
  if (!parseVersion(current, currentParts, currentSuffix) || !parseVersion(latest, latestParts, latestSuffix) ||
      *latestSuffix != '\0') {
    return false;
  }
  for (size_t i = 0; i < 3; ++i) {
    if (currentParts[i] != latestParts[i]) return latestParts[i] > currentParts[i];
  }
  return strncmp(currentSuffix, "-dev", 4) == 0 || strncmp(currentSuffix, "-rc", 3) == 0;
}

inline bool formatAssetName(char* output, size_t capacity, const char* tag, const char* board, size_t boardLength) {
  uint32_t parts[3] = {};
  const char* suffix = nullptr;
  if (!parseVersion(tag, parts, suffix) || *suffix != '\0') return false;
  const bool combinedC3 = boardLength == 2 && memcmp(board, "x4", 2) == 0;
  const int count = snprintf(output, capacity, "buddypoint-%s-%.*s.bin", withoutTagPrefix(tag),
                             combinedC3 ? 5 : static_cast<int>(boardLength), combinedC3 ? "x3-x4" : board);
  return count > 0 && static_cast<size_t>(count) < capacity;
}

}  // namespace ota_release
