#include "Ao3Bridge.h"

#include <AtomicFile.h>
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <fcntl.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace {
constexpr const char* CONFIG_PATH = "/.crosspoint/ao3-bridge.json";
constexpr size_t MAX_JSON = 24576;
ao3::Error apiError(const std::string& code) {
  if (code == "sign_in") return ao3::Error::SignIn;
  if (code == "rate_limit" || code == "busy") return ao3::Error::RateLimit;
  if (code == "missing") return ao3::Error::Missing;
  if (code == "storage") return ao3::Error::StorageError;
  if (code == "parse") return ao3::Error::Parse;
  if (code == "timeout") return ao3::Error::Timeout;
  if (code == "server") return ao3::Error::Server;
  return ao3::Error::Network;
}
bool hexId(const std::string& id) {
  return id.size() == 32 &&
         std::all_of(id.begin(), id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
}  // namespace

bool Ao3BridgeConfig::validUrl(const std::string& url) {
  if (url.empty()) return true;
  const size_t prefix = url.starts_with("https://") ? 8 : url.starts_with("http://") ? 7 : 0;
  if (!prefix || url.size() > 240 || url.size() <= prefix || url.back() == '/') return false;
  if (url.find("..", prefix) != std::string::npos) return false;
  for (size_t i = prefix; i < url.size(); ++i) {
    const auto c = url[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
          c == '_' || c == ':' || c == '/'))
      return false;
  }
  const auto slash = url.find('/', prefix);
  const auto authority = url.substr(prefix, slash == std::string::npos ? slash : slash - prefix);
  if (authority.empty() || authority.front() == ':' || authority.front() == '.' || authority.front() == '-')
    return false;
  const auto colon = authority.find(':');
  if (colon != std::string::npos) {
    const auto port = authority.substr(colon + 1);
    if (port.empty() || port.size() > 5 ||
        !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; }))
      return false;
    const int number = std::atoi(port.c_str());
    if (number < 1 || number > 65535) return false;
  }
  return true;
}
bool Ao3BridgeConfig::validToken(const std::string& token) {
  return token.empty() ||
         (token.size() >= 32 && token.size() <= 128 && std::all_of(token.begin(), token.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
          }));
}
std::string Ao3BridgeConfig::urlFor(const std::string& ssid) const {
  for (const auto& [name, url] : networks)
    if (name == ssid) return url;
  return defaultUrl;
}
bool Ao3BridgeConfig::setNetwork(const std::string& ssid, std::string url) {
  while (!url.empty() && url.back() == '/') url.pop_back();
  if (ssid.empty() || ssid.size() > 32 || !validUrl(url)) return false;
  for (auto it = networks.begin(); it != networks.end(); ++it) {
    if (it->first != ssid) continue;
    if (url.empty())
      networks.erase(it);
    else
      it->second = std::move(url);
    return true;
  }
  if (url.empty()) return true;
  if (networks.size() >= 8) return false;
  networks.reserve(8);
  networks.emplace_back(ssid, std::move(url));
  return true;
}
bool Ao3BridgeConfig::load() {
  *this = {};
  // An unreadable saved configuration must not switch the reader to direct AO3.
  enabled = Storage.exists(CONFIG_PATH);
  if (!atomic_file::recover(CONFIG_PATH)) return false;
  enabled = Storage.exists(CONFIG_PATH);
  HalFile file;
  if (!Storage.openFileForRead("AO3BRIDGE", CONFIG_PATH, file) || file.size() > 8192) return false;
  file.close();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(CONFIG_PATH, doc)) return false;
  if (!doc["enabled"].is<bool>() || !doc["default_url"].is<const char*>() || !doc["token_obf"].is<const char*>())
    return false;
  enabled = doc["enabled"] | false;
  defaultUrl = doc["default_url"] | "";
  bool ok = false, tooLong = false;
  token = obfuscation::deobfuscateFromBase64(doc["token_obf"] | "", 128, &ok, &tooLong);
  if (!validUrl(defaultUrl) || !ok || tooLong || !validToken(token)) {
    *this = {};
    enabled = true;
    return false;
  }
  for (JsonObjectConst item : doc["networks"].as<JsonArrayConst>()) {
    if (!setNetwork(item["ssid"] | "", item["url"] | "")) {
      *this = {};
      enabled = true;
      return false;
    }
  }
  return true;
}
bool Ao3BridgeConfig::save() const {
  if (!validUrl(defaultUrl) || !validToken(token) || networks.size() > 8) return false;
  JsonDocument doc;
  doc["enabled"] = enabled;
  doc["default_url"] = defaultUrl;
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
  auto array = doc["networks"].to<JsonArray>();
  for (const auto& [ssid, url] : networks) {
    if (ssid.empty() || ssid.size() > 32 || url.empty() || !validUrl(url)) return false;
    auto item = array.add<JsonObject>();
    item["ssid"] = ssid;
    item["url"] = url;
  }
  const auto temporary = std::string(CONFIG_PATH) + ".part";
  return Storage.ensureDirectoryExists("/.crosspoint") && atomic_file::recover(CONFIG_PATH) &&
         PersistableStoreBase::writeDocToFile(temporary.c_str(), doc) && atomic_file::publish(CONFIG_PATH, temporary);
}
bool Ao3Bridge::pulse(size_t bytes, size_t total) {
  if (!progress || progress(progressContext, bytes, total)) return true;
  error = ao3::Error::Cancelled;
  return false;
}
bool Ao3Bridge::transfer(const std::string& path, JsonDocument* result, HalFile* file, const std::string* body,
                         size_t expected) {
  if (baseUrl.empty() || !Ao3BridgeConfig::validUrl(baseUrl) || config.token.empty() ||
      !Ao3BridgeConfig::validToken(config.token) || !path.starts_with("/api/v1/") ||
      config.urlFor(WiFi.SSID().c_str()) != baseUrl) {
    error = ao3::Error::BridgeConfig;
    return false;
  }
  const bool secure = baseUrl.starts_with("https://");
  if (secure && time(nullptr) < 1704067200) {
    halClock.syncFromNTP();
    if (time(nullptr) < 1704067200) {
      error = ao3::Error::Clock;
      return false;
    }
  }
  auto cfg = makeUniqueNoThrow<esp_http_client_config_t>();
  auto buffer = makeUniqueNoThrow<char[]>(1024);
  if (!cfg || !buffer) {
    error = ao3::Error::Memory;
    return false;
  }
  const auto url = baseUrl + path;
  cfg->url = url.c_str();
  cfg->timeout_ms = 10000;
  cfg->buffer_size = 1024;
  cfg->buffer_size_tx = 1024;
  cfg->disable_auto_redirect = true;
  if (secure) cfg->crt_bundle_attach = esp_crt_bundle_attach;
  auto http = esp_http_client_init(cfg.get());
  if (!http) {
    error = ao3::Error::Memory;
    return false;
  }
  ScopedCleanup cleanup{[&http] {
    if (http) esp_http_client_cleanup(http);
  }};
  const auto auth = "Bearer " + config.token;
  if (esp_http_client_set_header(http, "Authorization", auth.c_str()) != ESP_OK ||
      esp_http_client_set_header(http, "Accept-Encoding", "identity") != ESP_OK) {
    error = ao3::Error::Memory;
    return false;
  }
  if (body) {
    esp_http_client_set_method(http, HTTP_METHOD_POST);
    if (esp_http_client_set_header(http, "Content-Type", "application/json") != ESP_OK) {
      error = ao3::Error::Memory;
      return false;
    }
  }
  if (!pulse()) return false;
  if (esp_http_client_open(http, body ? body->size() : 0) != ESP_OK ||
      (body && esp_http_client_write(http, body->data(), body->size()) != static_cast<int>(body->size()))) {
    error = ao3::Error::Network;
    return false;
  }
  const int socket = esp_http_client_get_socket(http);
  const int flags = socket >= 0 ? fcntl(socket, F_GETFL, 0) : -1;
  if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
    error = ao3::Error::Network;
    return false;
  }
  esp_http_client_set_timeout_ms(http, 100);
  auto lastData = millis();
  int64_t length;
  do {
    if (!pulse()) return false;
    if (millis() - lastData >= 30000) {
      error = ao3::Error::Timeout;
      return false;
    }
    length = esp_http_client_fetch_headers(http);
    if (length == -ESP_ERR_HTTP_EAGAIN) delay(25);
  } while (length == -ESP_ERR_HTTP_EAGAIN);
  const auto status = esp_http_client_get_status_code(http);
  if (length < 0 || (status != 200 && status != 202)) {
    // Never forward a device token through an HTTP redirect.
    error = status == 401 || status == 403 ? ao3::Error::BridgeConfig
            : status == 429                ? ao3::Error::RateLimit
                                           : ao3::Error::Network;
    return false;
  }
  const size_t limit = file ? 64 * 1024 * 1024 : MAX_JSON;
  if (length <= 0 || length > static_cast<int64_t>(limit) || (file && static_cast<size_t>(length) != expected)) {
    error = ao3::Error::Parse;
    return false;
  }
  std::string json;
  if (result) {
    if (ESP.getFreeHeap() < static_cast<size_t>(length) + 4096 ||
        ESP.getMaxAllocHeap() <= static_cast<size_t>(length)) {
      error = ao3::Error::Memory;
      return false;
    }
    json.reserve(static_cast<size_t>(length));
  }
  size_t received = 0;
  lastData = millis();
  while (true) {
    if (!pulse(received, length > 0 ? length : 0)) return false;
    if (millis() - lastData >= 30000) {
      error = ao3::Error::Timeout;
      return false;
    }
    const int n = esp_http_client_read(http, buffer.get(), 1024);
    if (n == -ESP_ERR_HTTP_EAGAIN) {
      delay(25);
      continue;
    }
    if (n < 0) {
      error = ao3::Error::Network;
      return false;
    }
    if (!n) break;
    received += n;
    lastData = millis();
    if (received > limit || received > static_cast<size_t>(length)) {
      error = ao3::Error::Parse;
      return false;
    }
    if (file && file->write(reinterpret_cast<uint8_t*>(buffer.get()), n) != static_cast<size_t>(n)) {
      error = ao3::Error::StorageError;
      return false;
    }
    if (result) json.append(buffer.get(), n);
  }
  if (!esp_http_client_is_complete_data_received(http) || (file && received != expected)) {
    error = ao3::Error::Network;
    return false;
  }
  esp_http_client_cleanup(http);
  http = nullptr;
  if (result && deserializeJson(*result, json)) {
    error = ao3::Error::Parse;
    return false;
  }
  return true;
}
bool Ao3Bridge::job(JsonDocument& args, JsonDocument& result) {
  error = ao3::Error::None;
  baseUrl = config.urlFor(WiFi.SSID().c_str());
  std::string body;
  serializeJson(args, body);
  args.clear();
  const bool accepted = transfer("/api/v1/jobs", &result, nullptr, &body);
  std::fill(body.begin(), body.end(), '\0');
  if (!accepted) return false;
  const std::string id = result["job"] | "";
  if (!hexId(id)) {
    error = ao3::Error::Parse;
    return false;
  }
  const auto start = millis();
  while (millis() - start < 600000) {
    for (unsigned i = 0; i < 20; ++i) {
      if (!pulse()) return false;
      delay(25);
    }
    result.clear();
    if (!transfer("/api/v1/jobs/" + id, &result)) return false;
    const std::string state = result["state"] | "";
    if (state == "done") return true;
    if (state == "error") {
      error = apiError(result["error"] | "");
      return false;
    }
    if (state != "queued" && state != "running") {
      error = ao3::Error::Parse;
      return false;
    }
  }
  error = ao3::Error::Timeout;
  return false;
}
bool Ao3Bridge::readWork(JsonVariantConst value, ao3::Work& work) {
  const std::string id = value["id"] | "";
  std::string parsed;
  if (!ao3::workId(id, parsed) || parsed != id) return false;
  work = {};
  work.id = id;
  work.title = std::string(value["title"] | "").substr(0, 192);
  work.author = std::string(value["author"] | "").substr(0, 192);
  work.summary = std::string(value["summary"] | "").substr(0, 384);
  work.fandom = std::string(value["fandom"] | "").substr(0, 192);
  work.rating = std::string(value["rating"] | "").substr(0, 80);
  work.warnings = std::string(value["warnings"] | "").substr(0, 384);
  work.tags = std::string(value["tags"] | "").substr(0, 192);
  work.updated = std::string(value["updated"] | "").substr(0, 32);
  work.chapters = value["chapters"] | 0U;
  work.totalChapters = value["total"] | 0U;
  work.words = value["words"] | 0U;
  return !work.title.empty();
}
bool Ao3Bridge::login(const std::string& user, const std::string& password) {
  JsonDocument args, result;
  args["operation"] = "login";
  args["username"] = user;
  args["password"] = password;
  return job(args, result);
}
bool Ao3Bridge::logout() {
  JsonDocument args, result;
  args["operation"] = "logout";
  return job(args, result);
}
bool Ao3Bridge::fetchWork(const std::string& id, ao3::Work& work) {
  JsonDocument args, result;
  args["operation"] = "work";
  args["id"] = id;
  if (!job(args, result)) return false;
  if (!readWork(result["result"], work) || work.id != id) {
    error = ao3::Error::Parse;
    return false;
  }
  return true;
}
bool Ao3Bridge::fetchList(const std::string& url, std::vector<ao3::Work>& works, std::string& next) {
  JsonDocument args, result;
  args["operation"] = "list";
  args["url"] = url;
  if (!job(args, result)) return false;
  const auto items = result["result"]["works"].as<JsonArrayConst>();
  if (items.isNull() || items.size() > 5) {
    error = ao3::Error::Parse;
    return false;
  }
  works.clear();
  works.reserve(items.size());
  for (JsonVariantConst item : items) {
    works.emplace_back();
    if (!readWork(item, works.back())) {
      error = ao3::Error::Parse;
      return false;
    }
  }
  next = result["result"]["next"] | "";
  if (!next.empty() && !ao3::trustedUrl(next)) {
    error = ao3::Error::Parse;
    return false;
  }
  return true;
}
bool Ao3Bridge::download(const std::string& id, HalFile& file, ao3::Work& work) {
  JsonDocument args, result;
  args["operation"] = "prepare";
  args["id"] = id;
  if (!job(args, result)) return false;
  const std::string path = result["result"]["path"] | "";
  const size_t size = result["result"]["size"] | 0U;
  if (!path.starts_with("/api/v1/files/") || !hexId(path.substr(14)) || !size || size > 64 * 1024 * 1024 ||
      !readWork(result["result"]["work"], work) || work.id != id) {
    error = ao3::Error::Parse;
    return false;
  }
  result.clear();
  return transfer(path, nullptr, &file, nullptr, size);
}
