#pragma once

#include <Ao3.h>
#include <ArduinoJson.h>
#include <HalStorage.h>

struct Ao3BridgeConfig {
  bool enabled = false;
  std::string defaultUrl, token;
  std::vector<std::pair<std::string, std::string>> networks;
  static bool validUrl(const std::string& url);
  static bool validToken(const std::string& token);
  std::string urlFor(const std::string& ssid) const;
  bool setNetwork(const std::string& ssid, std::string url);
  bool load();
  bool save() const;
};

class Ao3Bridge {
 public:
  Ao3BridgeConfig config;
  ao3::Error error = ao3::Error::None;
  using Progress = bool (*)(void*, size_t, size_t);
  void setProgress(Progress callback, void* context) {
    progress = callback;
    progressContext = context;
  }
  bool login(const std::string& user, const std::string& password);
  bool logout();
  bool fetchWork(const std::string& id, ao3::Work& work);
  bool fetchList(const std::string& url, std::vector<ao3::Work>& works, std::string& next);
  bool download(const std::string& id, HalFile& file, ao3::Work& work);

 private:
  Progress progress = nullptr;
  void* progressContext = nullptr;
  std::string baseUrl;
  bool pulse(size_t bytes = 0, size_t total = 0);
  bool job(JsonDocument& args, JsonDocument& result);
  bool transfer(const std::string& path, JsonDocument* result, HalFile* file = nullptr,
                const std::string* body = nullptr, size_t expected = 0);
  static bool readWork(JsonVariantConst value, ao3::Work& work);
};
