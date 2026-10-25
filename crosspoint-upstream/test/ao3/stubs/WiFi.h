#pragma once
#include <string>
namespace ao3test {
inline std::string ssid = "Home";
}
struct FakeWifi {
  std::string SSID() const { return ao3test::ssid; }
};
inline FakeWifi WiFi;
