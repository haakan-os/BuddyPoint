#pragma once
#include <HalStorage.h>

#include <string>

namespace atomic_file {
// These path allocations occur once per save/transfer, outside rendering and input polling.
inline bool recover(const std::string& path) {
  const auto backup = path + ".buddy-backup";
  if (!Storage.exists(backup.c_str())) return true;
  if (!Storage.exists(path.c_str())) return Storage.rename(backup.c_str(), path.c_str());
  return Storage.remove(backup.c_str());
}
inline bool publish(const std::string& path, const std::string& temporary) {
  const auto backup = path + ".buddy-backup";
  if (Storage.exists(backup.c_str())) return false;
  const bool existed = Storage.exists(path.c_str());
  if (existed && !Storage.rename(path.c_str(), backup.c_str())) return false;
  if (!Storage.rename(temporary.c_str(), path.c_str())) {
    if (existed) Storage.rename(backup.c_str(), path.c_str());
    return false;
  }
  if (existed) Storage.remove(backup.c_str());
  return true;
}
}  // namespace atomic_file
