#pragma once
#include <string>
// Persistence tests exercise session handling, not hardware-derived obfuscation.
namespace obfuscation {
inline std::string obfuscateToBase64(const std::string& value) { return "test:" + value; }
inline std::string deobfuscateFromBase64(const std::string& value, size_t limit, bool* ok, bool* tooLong) {
  *ok = value.starts_with("test:");
  *tooLong = value.size() > limit + 5;
  return *ok && !*tooLong ? value.substr(5) : "";
}
}  // namespace obfuscation
