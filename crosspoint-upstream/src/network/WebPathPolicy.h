#pragma once

#include <HalStorage.h>
#include <Memory.h>

#include <cctype>
#include <string>
#include <string_view>

namespace web_path {
inline bool protectedName(std::string_view name) {
  if (name.empty()) return false;
  if (name.front() == '.') return true;
  std::string lower(name);
  for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower == "system volume information" || lower == "xtcache";
}
// Check each resolved ancestor too: FAT short aliases can hide a leading dot.
inline bool protectedPath(std::string_view path) {
  if (path.empty() || path.front() != '/' || path.size() > 1024) return true;
  for (const unsigned char c : path)
    if (c < 32 || c == 127 || c == '\\') return true;
  std::string prefix;
  prefix.reserve(path.size());
  // Long FAT names exceed the stack budget. Reuse one bounded buffer per check.
  auto name = makeUniqueNoThrow<char[]>(256);
  if (!name) return true;
  size_t start = 1;
  while (start < path.size()) {
    auto end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    const auto component = path.substr(start, end - start);
    if (!component.empty()) {
      if (protectedName(component)) return true;
      prefix += '/';
      prefix += component;
      auto file = Storage.open(prefix.c_str());
      if (file) {
        name[0] = 0;
        file.getName(name.get(), 256);
        if (!name[0] || protectedName(name.get())) return true;
      } else if (Storage.exists(prefix.c_str()))
        return true;
    }
    start = end + 1;
  }
  return false;
}
}  // namespace web_path
