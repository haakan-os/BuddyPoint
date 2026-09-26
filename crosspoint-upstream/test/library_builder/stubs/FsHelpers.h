#pragma once

#include <cctype>
#include <string>

namespace FsHelpers {
inline bool checkFileExtension(const std::string& path, const char* extension) {
  const std::string suffix(extension);
  if (path.size() < suffix.size()) return false;
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[path.size() - suffix.size() + i])) != suffix[i]) return false;
  }
  return true;
}
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
inline bool hasMarkdownExtension(const std::string& path) {
  return checkFileExtension(path, ".md") || checkFileExtension(path, ".markdown");
}
}  // namespace FsHelpers
