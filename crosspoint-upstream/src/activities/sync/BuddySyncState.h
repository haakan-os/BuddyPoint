#pragma once

#include <Utf8.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Fixed storage: a long transfer session must not grow the log's heap usage.
class BuddySyncLog {
 public:
  struct Entry {
    char text[160] = {};
    bool success = false;
  };
  static constexpr size_t capacity = 8;

  void clear() { start = count = 0; }
  size_t size() const { return count; }
  const Entry& operator[](size_t index) const { return entries[(start + index) % capacity]; }
  void add(const char* text, bool success) {
    const size_t index = (start + count) % capacity;
    snprintf(entries[index].text, sizeof(entries[index].text), "%s", text);
    auto& stored = entries[index].text;
    stored[utf8SafeTruncateBuffer(stored, static_cast<int>(std::strlen(stored)))] = '\0';
    entries[index].success = success;
    if (count < capacity)
      ++count;
    else
      start = (start + 1) % capacity;
  }

 private:
  std::array<Entry, capacity> entries{};
  size_t start = 0;
  size_t count = 0;
};

// Tracks attempts rather than names, including transfers completed inside one
// blocking HTTP call and repeated uploads of the same book.
class BuddySyncTransferState {
 public:
  enum class Update { None, Progress, Succeeded, Failed };
  Update observe(uint32_t attempt, bool inProgress, bool success, uint32_t now, size_t bytes, size_t total) {
    if (attempt == 0) return Update::None;
    if (attempt != currentAttempt) {
      currentAttempt = attempt;
      finished = false;
      lastRefresh = now;
      percent = percentage(bytes, total);
      if (inProgress) return Update::Progress;
    }
    if (finished) return Update::None;
    if (!inProgress) {
      finished = true;
      percent = success ? 100 : 0;
      return success ? Update::Succeeded : Update::Failed;
    }
    if (static_cast<uint32_t>(now - lastRefresh) < 1500) return Update::None;
    lastRefresh = now;
    percent = percentage(bytes, total);
    return Update::Progress;
  }
  int getPercent() const { return percent; }

 private:
  static int percentage(size_t bytes, size_t total) {
    // Content-Length includes multipart framing: reserve 100% for success.
    return total ? static_cast<int>(std::min<uint64_t>(99, static_cast<uint64_t>(bytes) * 100 / total)) : 0;
  }
  uint32_t currentAttempt = 0;
  uint32_t lastRefresh = 0;
  int percent = 0;
  bool finished = false;
};
