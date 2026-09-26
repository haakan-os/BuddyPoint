#include "MarkdownChecklist.h"

#include <AtomicFile.h>
#include <BufferedFile.h>
#include <HalStorage.h>

#include <cstring>
#include <string>

namespace markdown {
namespace {
uint32_t hashByte(uint32_t hash, uint8_t byte) { return (hash ^ byte) * 16777619u; }
}  // namespace
bool Checklist::load(const char* path) {
  count = 0;
  truncated = false;
  if (!atomic_file::recover(path)) return false;
  HalFile file;
  if (!Storage.openFileForRead("TASKS", path, file)) return false;
  fileSize = file.size();
  // One 512-byte read buffer avoids a storage lock/transaction for every character.
  serialization::BufferedFileReader input(file, 512);
  fingerprint = 2166136261u;
  size_t position = 0;
  char fence = 0;
  size_t fenceLength = 0;
  while (position < fileSize) {
    const size_t start = position;
    size_t length = 0;
    while (position < fileSize) {
      uint8_t byte;
      if (input.read(&byte, 1) != 1) return false;
      ++position;
      if (position % 4096 == 0) vTaskDelay(1);
      fingerprint = hashByte(fingerprint, byte);
      if (byte == '\n') break;
      if (length + 1 < sizeof(line)) line[length++] = static_cast<char>(byte);
    }
    // Only the prefix is needed for markers and labels; consume every byte above.
    line[length] = '\0';
    size_t i = start == 0 && length >= 3 && memcmp(line, "\xef\xbb\xbf", 3) == 0 ? 3 : 0;
    size_t indent = 0;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) indent += line[i++] == '\t' ? 4 : 1;
    if (indent >= 4) continue;
    if (i < length && (line[i] == '`' || line[i] == '~')) {
      const char c = line[i];
      size_t end = i;
      while (end < length && line[end] == c) ++end;
      if (!fence && end - i >= 3) {
        fence = c;
        fenceLength = end - i;
        continue;
      }
      if (fence == c && end - i >= fenceLength) {
        while (end < length && (line[end] == ' ' || line[end] == '\t' || line[end] == '\r')) ++end;
        if (end == length) fence = 0;
      }
      continue;
    }
    if (fence) continue;
    if (i < length && (line[i] == '-' || line[i] == '+' || line[i] == '*'))
      ++i;
    else {
      const size_t first = i;
      while (i < length && line[i] >= '0' && line[i] <= '9') ++i;
      if (i == first || i >= length || (line[i] != '.' && line[i] != ')')) continue;
      ++i;
    }
    if (i >= length || (line[i] != ' ' && line[i] != '\t')) continue;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i + 3 > length || line[i] != '[' || line[i + 2] != ']' ||
        (line[i + 1] != ' ' && line[i + 1] != 'x' && line[i + 1] != 'X'))
      continue;
    if (i + 3 < length && line[i + 3] != ' ' && line[i + 3] != '\t' && line[i + 3] != '\r') continue;
    if (count == MAX_TASKS) {
      truncated = true;
      continue;
    }
    auto& task = tasks[count++];
    task.offset = start + i + 1;
    task.marker = line[i + 1];
    i += 3;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) ++i;
    size_t labelLength = length - i;
    if (labelLength && line[length - 1] == '\r') --labelLength;
    if (labelLength >= sizeof(task.label) - 4) {
      labelLength = sizeof(task.label) - 5;
      while (labelLength && (static_cast<uint8_t>(line[i + labelLength]) & 0xc0) == 0x80) --labelLength;
    }
    memcpy(task.label, task.marker == ' ' ? "[ ] " : "[x] ", 4);
    memcpy(task.label + 4, line + i, labelLength);
    task.label[4 + labelLength] = '\0';
  }
  return true;
}

bool Checklist::toggle(const char* path, size_t index) {
  if (index >= count) return false;
  const auto& task = tasks[index];
  const char replacement = task.marker == ' ' ? 'x' : ' ';
  // Copy through a bounded buffer; never truncate the source to save one marker.
  const std::string temporary = std::string(path) + ".buddy-part";
  HalFile source, output;
  if (!Storage.openFileForRead("TASKS", path, source) || source.size() != fileSize ||
      !Storage.openFileForWrite("TASKS", temporary, output))
    return false;
  uint32_t hash = 2166136261u;
  uint32_t newHash = 2166136261u;
  size_t position = 0;
  bool ok = true;
  while (position < fileSize && ok) {
    const int read = source.read(line, sizeof(line));
    if (read <= 0) {
      ok = false;
      break;
    }
    for (int i = 0; i < read; ++i) {
      hash = hashByte(hash, static_cast<uint8_t>(line[i]));
      if (position + i == task.offset) {
        if (line[i] != task.marker) ok = false;
        line[i] = replacement;
      }
      newHash = hashByte(newHash, static_cast<uint8_t>(line[i]));
    }
    if (output.write(line, read) != static_cast<size_t>(read)) ok = false;
    position += read;
    if (position % 4096 == 0) vTaskDelay(1);
  }
  ok = output.close() && ok;
  source.close();
  ok = ok && position == fileSize && hash == fingerprint;
  if (ok) ok = atomic_file::publish(path, temporary);
  Storage.remove(temporary.c_str());
  if (!ok) return false;
  fingerprint = newHash;
  tasks[index].marker = replacement;
  tasks[index].label[1] = replacement;
  return true;
}
}  // namespace markdown
