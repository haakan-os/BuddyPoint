#pragma once
#include <ArduinoJson.h>
#include <HalStorage.h>
class PersistableStoreBase {
 public:
  static bool readDocFromFile(const char* path, JsonDocument& doc) {
    if (!ao3test::files.contains(path)) return false;
    const auto& data = ao3test::files.at(path);
    return !deserializeJson(doc, data.data(), data.size());
  }
  static bool writeDocToFile(const char* path, const JsonDocument& doc) {
    std::string data;
    serializeJson(doc, data);
    HalFile file;
    return Storage.openFileForWrite("AO3", path, file) && file.write(data.data(), data.size()) == data.size() &&
           file.close();
  }
};
