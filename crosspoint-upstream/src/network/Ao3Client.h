#pragma once

#include <Ao3.h>
#include <HalStorage.h>

#include "Ao3Bridge.h"

class Ao3Client {
 public:
  using Error = ao3::Error;
  using Progress = bool (*)(void* context, size_t downloaded, size_t total);
  void setProgress(Progress callback, void* context) {
    progress = callback;
    progressContext = context;
    bridge.setProgress(callback, context);
  }
  bool loadSession();
  bool signIn(const std::string& username, const std::string& password);
  bool signOut();
  bool signedIn() const { return bridge.config.enabled || (!username.empty() && !cookies.header().empty()); }
  Ao3BridgeConfig& bridgeConfig() { return bridge.config; }
  bool usesBridge() const { return bridge.config.enabled; }
  const std::string& account() const { return username; }
  Error error() const { return lastError; }
  bool fetchWork(const std::string& id, ao3::Work& work);
  bool fetchList(const std::string& url, bool requireSignIn, std::vector<ao3::Work>& works, std::string& next);
  bool download(const ao3::Work& work);
  bool readDownloaded(const std::string& id, ao3::Work& work);
  static bool markOpened(const std::string& path);
  bool downloadedPage(size_t offset, std::vector<ao3::Work>& works, bool& more);
  bool snapshotDownloads();
  static constexpr const char* UPDATE_QUEUE = "/.crosspoint/ao3-update-queue.txt";
  static std::string bookPath(const std::string& id);

 private:
  Ao3Bridge bridge;
  ao3::Cookies cookies;
  std::string username;
  std::string savedCookies;
  Error lastError = Error::None;
  Progress progress = nullptr;
  void* progressContext = nullptr;
  unsigned long lastRequest = 0;
  bool saveSession();
  bool saveWork(const ao3::Work& work);
  bool request(std::string url, ao3::Parser* parser, HalFile* file = nullptr, const std::string* post = nullptr);
  bool pulse(size_t downloaded = 0, size_t total = 0);
};
