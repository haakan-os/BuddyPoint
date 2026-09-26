#pragma once
#include <cstddef>
#include <cstdint>

namespace markdown {
class Checklist {
 public:
  static constexpr size_t MAX_TASKS = 128;
  struct Task {
    size_t offset = 0;
    char label[100]{};
    char marker = ' ';
  };
  // Activity-owned fixed storage avoids per-row allocations on a constrained device.
  Task tasks[MAX_TASKS]{};
  size_t count = 0;
  bool truncated = false;
  bool load(const char* path);
  bool toggle(const char* path, size_t index);

 private:
  char line[512]{};
  uint32_t fingerprint = 0;
  size_t fileSize = 0;
};
}  // namespace markdown
