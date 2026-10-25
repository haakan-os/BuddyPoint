#pragma once
#include <string>
class Epub {
 public:
  Epub(const std::string& path, const std::string& root) : cache(root + "/cache-" + path.substr(path.rfind('/') + 1)) {}
  const std::string& getCachePath() const { return cache; }

 private:
  std::string cache;
};
