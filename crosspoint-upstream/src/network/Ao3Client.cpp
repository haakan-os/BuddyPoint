#include "Ao3Client.h"

#include <AtomicFile.h>
#include <Epub.h>
#include <HalClock.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>
#include <PersistableStore.h>
#include <ZipFile.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_wifi.h>
#include <fcntl.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
constexpr const char* SESSION_PATH = "/.crosspoint/ao3-session.json";
constexpr const char* RECORDS_PATH = "/.crosspoint/ao3";
constexpr size_t MAX_DOWNLOAD = 64 * 1024 * 1024;
constexpr uint32_t REQUEST_GAP_MS = 2000;
constexpr uint32_t RESPONSE_IDLE_MS = 30000;
constexpr uint32_t HTML_RESPONSE_MS = 120000;

bool smallDocument(const std::string& path, JsonDocument& doc, size_t limit) {
  if (!atomic_file::recover(path)) return false;
  {
    HalFile file;
    if (!Storage.openFileForRead("AO3", path, file) || file.size() > limit) return false;
  }
  return PersistableStoreBase::readDocFromFile(path.c_str(), doc);
}
bool writeDocument(const std::string& path, const JsonDocument& doc) {
  if (!atomic_file::recover(path)) return false;
  const auto temporary = path + ".part";
  if (!PersistableStoreBase::writeDocToFile(temporary.c_str(), doc)) return false;
  return atomic_file::publish(path, temporary);
}
std::string recordPath(const std::string& id) { return std::string(RECORDS_PATH) + "/" + id + ".json"; }
bool validId(const std::string& id) {
  std::string parsed;
  return ao3::workId(id, parsed) && parsed == id;
}

// Keep reading position and the separate BookmarkFile store. Derived spine,
// layout and cover caches must be rebuilt after replacing a downloaded EPUB.
bool invalidateDerivedCache(const std::string& path) {
  const auto cache = Epub(path, "/.crosspoint").getCachePath();
  if (!Storage.exists(cache.c_str())) return true;
  auto dir = Storage.open(cache.c_str());
  if (!dir || !dir.isDirectory()) return false;
  while (auto file = dir.openNextFile()) {
    char name[128]{};
    file.getName(name, sizeof(name));
    if (std::strcmp(name, "progress.bin") == 0) continue;
    const bool directory = file.isDirectory();
    const auto child = cache + "/" + name;
    file.close();
    if (!(directory ? Storage.removeDir(child.c_str()) : Storage.remove(child.c_str()))) return false;
  }
  return true;
}
struct Headers {
  ao3::Cookies* cookies;
  std::string location;
  bool invalidCookie = false;
  char server[32]{};
  char requestId[40]{};
  static void copyDiagnostic(char* out, size_t capacity, const char* value) {
    size_t i = 0;
    for (; i + 1 < capacity && value[i]; ++i) {
      const auto c = static_cast<unsigned char>(value[i]);
      out[i] = c >= 32 && c < 127 ? static_cast<char>(c) : '?';
    }
    out[i] = '\0';
  }
  static esp_err_t event(esp_http_client_event_t* event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    auto* self = static_cast<Headers*>(event->user_data);
    if (!strcasecmp(event->header_key, "Set-Cookie")) {
      if (!self->cookies->update(event->header_value)) self->invalidCookie = true;
    } else if (!strcasecmp(event->header_key, "Location")) {
      if (std::strlen(event->header_value) <= 2048) self->location = event->header_value;
    } else if (!strcasecmp(event->header_key, "Server")) {
      copyDiagnostic(self->server, sizeof(self->server), event->header_value);
    } else if (!strcasecmp(event->header_key, "CF-Ray")) {
      copyDiagnostic(self->requestId, sizeof(self->requestId), event->header_value);
    }
    return ESP_OK;
  }
};
struct Connection {
  esp_http_client_handle_t handle = nullptr;
  ~Connection() {
    if (handle) esp_http_client_cleanup(handle);
  }
};
struct WifiPower {
  wifi_ps_type_t previous = WIFI_PS_MIN_MODEM;
  WifiPower() {
    esp_wifi_get_ps(&previous);
    esp_wifi_set_ps(WIFI_PS_NONE);
  }
  ~WifiPower() { esp_wifi_set_ps(previous); }
};
}  // namespace

bool Ao3Client::pulse(size_t downloaded, size_t total) {
  if (!progress || progress(progressContext, downloaded, total)) return true;
  lastError = Error::Cancelled;
  return false;
}
bool Ao3Client::request(std::string url, ao3::Parser* parser, HalFile* file, const std::string* post) {
  lastError = Error::None;
  if (!ao3::trustedUrl(url)) {
    lastError = Error::Parse;
    return false;
  }
  if (!cookies.prepare()) {
    lastError = Error::Memory;
    return false;
  }
  if (time(nullptr) < 1704067200) {
    halClock.syncFromNTP();
    // X3/X4 may have no hardware RTC; TLS still needs a valid system clock.
    if (time(nullptr) < 1704067200) {
      const char* tz = getenv("TZ");
      const std::string previousTz = tz ? tz : "UTC0";
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      setenv("TZ", previousTz.c_str(), 1);
      tzset();
      const auto start = millis();
      while (time(nullptr) < 1704067200 && millis() - start < 8000) {
        if (!pulse()) return false;
        delay(50);
      }
    }
    if (time(nullptr) < 1704067200) {
      lastError = Error::Clock;
      return false;
    }
  }
  WifiPower power;
  bool retriedUpstreamTls = false;
  uint32_t requestGap = REQUEST_GAP_MS;
  for (unsigned hop = 0; hop < 6; ++hop) {
    while (lastRequest && millis() - lastRequest < requestGap) {
      if (!pulse()) return false;
      delay(25);
    }
    requestGap = REQUEST_GAP_MS;
    if (!pulse()) return false;
    if (ESP.getFreeHeap() < 55000 || ESP.getMaxAllocHeap() < 22000) {
      lastError = Error::Memory;
      return false;
    }
    lastRequest = millis();
    // Config and the 1 KB streaming buffer are request-lifetime heap allocations
    // to keep both the main task stack and TLS peak memory bounded.
    auto config = makeUniqueNoThrow<esp_http_client_config_t>();
    auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
    if (!config || !buffer) {
      lastError = Error::Memory;
      return false;
    }
    Headers headers{&cookies, {}, false};
    config->url = url.c_str();
    config->timeout_ms = 20000;
    // esp_http_client reassembles fragmented headers before dispatching them.
    // A long Set-Cookie does not require an equally large receive buffer.
    config->buffer_size = 2048;
    config->buffer_size_tx = 6144;
    config->disable_auto_redirect = true;
    config->crt_bundle_attach = esp_crt_bundle_attach;
    config->event_handler = Headers::event;
    config->user_data = &headers;
    Connection connection{esp_http_client_init(config.get())};
    if (!connection.handle) {
      lastError = Error::Memory;
      return false;
    }
    auto http = connection.handle;
    esp_http_client_set_header(http, "User-Agent", "BuddyPoint/" CROSSPOINT_VERSION " (AO3 reader)");
    esp_http_client_set_header(http, "Accept-Encoding", "identity");
    esp_http_client_set_header(http, "Accept", file ? "application/epub+zip" : "text/html");
    if (!cookies.header().empty()) esp_http_client_set_header(http, "Cookie", cookies.header().data());
    if (post) {
      esp_http_client_set_method(http, HTTP_METHOD_POST);
      esp_http_client_set_header(http, "Content-Type", "application/x-www-form-urlencoded");
      esp_http_client_set_header(http, "Origin", ao3::ORIGIN);
      esp_http_client_set_header(http, "Referer", "https://archiveofourown.org/users/login");
    }
    LOG_INF("AO3", "Request hop %u: connecting (%s), heap %u, largest %u", hop + 1, post ? "POST" : "GET",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    const auto opened = esp_http_client_open(http, post ? post->size() : 0);
    if (opened != ESP_OK) {
      int tlsError = 0, verifyFlags = 0;
      const auto tlsResult = esp_http_client_get_and_clear_last_tls_error(http, &tlsError, &verifyFlags);
      LOG_ERR("AO3", "Connection failed: %d; TLS result %d, error %d, verify flags 0x%x", opened, tlsResult, tlsError,
              static_cast<unsigned>(verifyFlags));
      lastError = Error::Network;
      return false;
    }
    LOG_INF("AO3", "Connected; sending request and waiting for headers");
    if (post && esp_http_client_write(http, post->data(), post->size()) != static_cast<int>(post->size())) {
      lastError = Error::Network;
      return false;
    }
    // The HTTP timeout alone only bounds select(), not a partial TLS record's
    // subsequent recv(). Make both layers yield during response reads.
    const int socket = esp_http_client_get_socket(http);
    const int flags = socket >= 0 ? fcntl(socket, F_GETFL, 0) : -1;
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0 ||
        esp_http_client_set_timeout_ms(http, 250) != ESP_OK) {
      LOG_ERR("AO3", "Could not enable cooperative response reads");
      lastError = Error::Network;
      return false;
    }
    const auto responseStart = millis();
    int64_t length;
    do {
      if (!pulse()) return false;
      if (millis() - responseStart >= RESPONSE_IDLE_MS) {
        LOG_ERR("AO3", "Response header timeout");
        lastError = Error::Timeout;
        return false;
      }
      length = esp_http_client_fetch_headers(http);
      if (length == -ESP_ERR_HTTP_EAGAIN) delay(10);
    } while (length == -ESP_ERR_HTTP_EAGAIN);
    const int status = esp_http_client_get_status_code(http);
    LOG_INF("AO3", "Response headers: status %d, length %lld, heap %u", status, static_cast<long long>(length),
            ESP.getFreeHeap());
    if (length < 0) {
      lastError = Error::Network;
      return false;
    }
    if (headers.invalidCookie) {
      lastError = Error::SignIn;
      return false;
    }
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      std::string destination;
      if (!ao3::resolveUrl(url, headers.location, destination)) {
        lastError = Error::Network;
        return false;
      }
      if (file && destination.find("/users/login") != std::string::npos) {
        lastError = Error::SignIn;
        return false;
      }
      url = std::move(destination);
      if (status == 301 || status == 302 || status == 303) post = nullptr;
      continue;
    }
    if (status != 200) {
      if (status >= 500 && status <= 599)
        LOG_ERR("AO3", "Server error %d: server=%s request-id=%s", status, headers.server, headers.requestId);
      if (status == 525 && !post && !retriedUpstreamTls && hop + 1 < 6) {
        LOG_INF("AO3", "Server returned 525; retrying GET once in 5 seconds");
        retriedUpstreamTls = true;
        lastRequest = millis();
        requestGap = 5000;
        // Leaving this scope closes TLS before the cancellable retry wait.
        continue;
      }
      lastError = status == 429                    ? Error::RateLimit
                  : status == 404                  ? Error::Missing
                  : status == 401 || status == 403 ? Error::SignIn
                  : status >= 500 && status <= 599 ? Error::Server
                                                   : Error::Network;
      return false;
    }
    if (file && length > static_cast<int64_t>(MAX_DOWNLOAD)) {
      lastError = Error::StorageError;
      return false;
    }
    size_t received = 0;
    auto lastData = millis();
    // A zero poll timeout returns to the input/progress callback instead of
    // repeatedly waiting to fill a 1 KB read across slow TLS/HTTP chunks.
    esp_http_client_set_timeout_ms(http, 0);
    while (true) {
      if (!pulse(received, length > 0 ? length : 0)) return false;
      if (millis() - lastData >= RESPONSE_IDLE_MS || (!file && millis() - responseStart >= HTML_RESPONSE_MS)) {
        LOG_ERR("AO3", "Response body timeout after %u bytes", static_cast<unsigned>(received));
        lastError = Error::Timeout;
        return false;
      }
      const int n = esp_http_client_read(http, reinterpret_cast<char*>(buffer.get()), 1024);
      if (n == -ESP_ERR_HTTP_EAGAIN) {
        delay(25);
        continue;
      }
      if (n < 0) {
        LOG_ERR("AO3", "Response read failed: %d", n);
        lastError = Error::Network;
        return false;
      }
      if (n == 0) break;
      lastData = millis();
      received += n;
      if (file) {
        if (received > MAX_DOWNLOAD || file->write(buffer.get(), n) != static_cast<size_t>(n)) {
          lastError = Error::StorageError;
          return false;
        }
      } else if (parser && !parser->feed(buffer.get(), n)) {
        lastError = Error::Parse;
        return false;
      }
      if (parser && parser->authReady()) {
        LOG_INF("AO3", "Authentication markers received after %u bytes", static_cast<unsigned>(received));
        return true;
      }
    }
    if (!esp_http_client_is_complete_data_received(http) || received == 0) {
      LOG_ERR("AO3", "Incomplete response: %u bytes", static_cast<unsigned>(received));
      lastError = Error::Network;
      return false;
    }
    if (parser && !parser->finish()) {
      lastError = parser->loginForm ? Error::SignIn : Error::Parse;
      return false;
    }
    LOG_INF("AO3", "Response complete: %u bytes", static_cast<unsigned>(received));
    return true;
  }
  lastError = Error::Network;
  return false;
}
bool Ao3Client::loadSession() {
  cookies.clear();
  username.clear();
  savedCookies.clear();
  JsonDocument doc;
  if (!smallDocument(SESSION_PATH, doc, 8192)) return false;
  username = doc["username"] | "";
  bool ok = false, tooLong = false;
  auto value = obfuscation::deobfuscateFromBase64(doc["session_obf"] | "", ao3::MAX_COOKIE_BYTES, &ok, &tooLong);
  if (!ok || tooLong || username.size() > 80 || !cookies.restore(value)) {
    cookies.clear();
    username.clear();
    return false;
  }
  savedCookies = cookies.header();
  return signedIn();
}
bool Ao3Client::saveSession() {
  if (!signedIn() || savedCookies == cookies.header()) return true;
  JsonDocument doc;
  doc["username"] = username;
  doc["session_obf"] = obfuscation::obfuscateToBase64(std::string(cookies.header()));
  if (!writeDocument(SESSION_PATH, doc)) {
    lastError = Error::StorageError;
    return false;
  }
  savedCookies = cookies.header();
  return true;
}
bool Ao3Client::signOut() {
  cookies.clear();
  savedCookies.clear();
  username.clear();
  lastError = Error::None;
  for (const auto& suffix : {"", ".part", ".buddy-backup"}) {
    const auto path = std::string(SESSION_PATH) + suffix;
    if (Storage.exists(path.c_str()) && !Storage.remove(path.c_str())) {
      lastError = Error::StorageError;
      return false;
    }
  }
  return true;
}
bool Ao3Client::signIn(const std::string& user, const std::string& password) {
  LOG_INF("AO3", "Sign-in: requesting login form");
  if (user.empty() || user.size() > 80 || password.empty() || password.size() > 256) {
    lastError = Error::SignIn;
    return false;
  }
  if (!signOut()) return false;
  auto parser = makeUniqueNoThrow<ao3::Parser>(ao3::Parser::Mode::Auth);
  if (!parser) {
    lastError = Error::Memory;
    return false;
  }
  if (!request(std::string(ao3::ORIGIN) + "/users/login", parser.get()) || parser->csrf.empty()) {
    if (lastError == Error::None) lastError = Error::SignIn;
    return false;
  }
  std::string payload = "authenticity_token=" + ao3::encode(parser->csrf) + "&user[login]=" + ao3::encode(user) +
                        "&user[password]=" + ao3::encode(password) + "&user[remember_me]=1&commit=Log+in";
  parser = makeUniqueNoThrow<ao3::Parser>(ao3::Parser::Mode::Auth);
  if (!parser) {
    lastError = Error::Memory;
    return false;
  }
  LOG_INF("AO3", "Sign-in: submitting form");
  const bool fetched = request(std::string(ao3::ORIGIN) + "/users/login", parser.get(), nullptr, &payload);
  std::fill(payload.begin(), payload.end(), '\0');
  if (!fetched || !parser->signedIn || cookies.header().empty()) {
    cookies.clear();
    if (lastError == Error::None) lastError = Error::SignIn;
    return false;
  }
  username = user;
  LOG_INF("AO3", "Sign-in: saving session");
  return saveSession();
}
bool Ao3Client::fetchWork(const std::string& id, ao3::Work& work) {
  if (!validId(id)) {
    lastError = Error::Parse;
    return false;
  }
  auto parser = makeUniqueNoThrow<ao3::Parser>(ao3::Parser::Mode::Work, id);
  if (!parser) {
    lastError = Error::Memory;
    return false;
  }
  if (!request(std::string(ao3::ORIGIN) + "/works/" + id + "?view_adult=true", parser.get())) return false;
  work = std::move(parser->work);
  return saveSession();
}
bool Ao3Client::fetchList(const std::string& url, bool requireSignIn, std::vector<ao3::Work>& works,
                          std::string& next) {
  if (requireSignIn && !signedIn()) {
    lastError = Error::SignIn;
    return false;
  }
  auto parser = makeUniqueNoThrow<ao3::Parser>(ao3::Parser::Mode::Listing);
  if (!parser) {
    lastError = Error::Memory;
    return false;
  }
  if (!request(url, parser.get())) return false;
  if (requireSignIn && !parser->signedIn) {
    lastError = Error::SignIn;
    return false;
  }
  if (!parser->validListing && !parser->signedIn) {
    lastError = Error::Parse;
    return false;
  }
  next.clear();
  if (!parser->next.empty() && !ao3::resolveUrl(url, parser->next, next)) {
    lastError = Error::Parse;
    return false;
  }
  works = std::move(parser->works);
  return saveSession();
}
std::string Ao3Client::bookPath(const std::string& id) { return "/AO3/" + id + ".epub"; }
bool Ao3Client::saveWork(const ao3::Work& work) {
  if (!Storage.ensureDirectoryExists(RECORDS_PATH)) return false;
  JsonDocument doc;
  // Preserve the last-opened chapter count when refreshing remote metadata.
  smallDocument(recordPath(work.id), doc, 8192);
  doc["id"] = work.id;
  doc["title"] = work.title;
  doc["author"] = work.author;
  doc["updated"] = work.updated;
  doc["chapters"] = work.chapters;
  doc["total"] = work.totalChapters;
  doc["words"] = work.words;
  doc["fandom"] = work.fandom;
  doc["summary"] = work.summary;
  doc["rating"] = work.rating;
  doc["warnings"] = work.warnings;
  doc["tags"] = work.tags;
  doc["download"] = work.download;
  return writeDocument(recordPath(work.id), doc);
}
bool Ao3Client::readDownloaded(const std::string& id, ao3::Work& work) {
  if (!validId(id)) return false;
  const auto path = bookPath(id);
  if (!atomic_file::recover(path) || !Storage.exists(path.c_str())) return false;
  JsonDocument doc;
  if (!smallDocument(recordPath(id), doc, 8192)) return false;
  if (id != (doc["id"] | "")) return false;
  work.id = id;
  work.title = std::string(doc["title"] | "").substr(0, 192);
  work.author = std::string(doc["author"] | "").substr(0, 192);
  work.updated = std::string(doc["updated"] | "").substr(0, 32);
  work.summary = std::string(doc["summary"] | "").substr(0, 768);
  work.fandom = std::string(doc["fandom"] | "").substr(0, 192);
  work.rating = std::string(doc["rating"] | "").substr(0, 80);
  work.warnings = std::string(doc["warnings"] | "").substr(0, 384);
  work.tags = std::string(doc["tags"] | "").substr(0, 192);
  work.download = std::string(doc["download"] | "").substr(0, 2048);
  work.chapters = doc["chapters"] | 0U;
  work.totalChapters = doc["total"] | 0U;
  work.words = doc["words"] | 0U;
  work.readingStatus = ao3::ReadingStatus::Unread;
  const uint32_t openedChapters = doc["opened_chapters"] | 0U;
  if (openedChapters) work.readingStatus = ao3::ReadingStatus::Reading;
  const auto cache = Epub(path, "/.crosspoint").getCachePath();
  HalFile progress;
  if (Storage.openFileForRead("AO3", cache + "/progress.bin", progress)) {
    uint8_t position[4]{};
    if (progress.read(position, sizeof(position)) == sizeof(position)) {
      work.readingStatus = ao3::ReadingStatus::Reading;
      const uint16_t spine = position[0] | (position[1] << 8);
      progress.close();
      HalFile book;
      uint8_t header[7]{};
      if (Storage.openFileForRead("AO3", cache + "/book.bin", book) &&
          book.read(header, sizeof(header)) == sizeof(header)) {
        const uint16_t spines = header[5] | (header[6] << 8);
        if (spines > 0 && spine >= spines)
          work.readingStatus = work.complete() ? ao3::ReadingStatus::Finished : ao3::ReadingStatus::Waiting;
      }
    }
  }
  if (openedChapters && work.chapters > openedChapters) work.readingStatus = ao3::ReadingStatus::NewChapters;
  return true;
}
bool Ao3Client::markOpened(const std::string& path) {
  std::string id;
  if (!ao3::downloadedId(path, id)) return true;
  JsonDocument doc;
  if (!smallDocument(recordPath(id), doc, 8192) || id != (doc["id"] | "")) return false;
  const uint32_t chapters = doc["chapters"] | 0U;
  if ((doc["opened_chapters"] | 0U) == chapters) return true;
  doc["opened_chapters"] = chapters;
  return writeDocument(recordPath(id), doc);
}
bool Ao3Client::downloadedPage(size_t offset, std::vector<ao3::Work>& works, bool& more) {
  lastError = Error::None;
  works.clear();
  works.reserve(ao3::PAGE_SIZE);
  more = false;
  if (!Storage.exists(RECORDS_PATH)) return true;
  auto dir = Storage.open(RECORDS_PATH);
  if (!dir || !dir.isDirectory()) {
    lastError = Error::StorageError;
    return false;
  }
  size_t seen = 0;
  while (auto file = dir.openNextFile()) {
    if (file.isDirectory()) continue;
    char name[64]{};
    file.getName(name, sizeof(name));
    const std::string filename(name);
    if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;
    const auto id = filename.substr(0, filename.size() - 5);
    file.close();
    ao3::Work work;
    if (!readDownloaded(id, work)) continue;
    if (seen++ < offset) continue;
    if (works.size() == ao3::PAGE_SIZE) {
      more = true;
      break;
    }
    works.push_back(std::move(work));
  }
  return true;
}
bool Ao3Client::snapshotDownloads() {
  lastError = Error::None;
  if (!Storage.ensureDirectoryExists("/.crosspoint")) {
    lastError = Error::StorageError;
    return false;
  }
  if (Storage.exists(UPDATE_QUEUE) && !Storage.remove(UPDATE_QUEUE)) {
    lastError = Error::StorageError;
    return false;
  }
  HalFile output;
  if (!Storage.openFileForWrite("AO3", UPDATE_QUEUE, output)) {
    lastError = Error::StorageError;
    return false;
  }
  auto dir = Storage.open(RECORDS_PATH);
  if (!dir && Storage.exists(RECORDS_PATH)) {
    lastError = Error::StorageError;
    return false;
  }
  while (dir) {
    auto file = dir.openNextFile();
    if (!file) break;
    if (!pulse()) return false;
    if (file.isDirectory()) continue;
    char name[64]{};
    file.getName(name, sizeof(name));
    const std::string filename(name);
    if (filename.size() < 6 || filename.substr(filename.size() - 5) != ".json") continue;
    const auto id = filename.substr(0, filename.size() - 5);
    if (!validId(id)) continue;
    const auto line = id + "\n";
    if (output.write(reinterpret_cast<const uint8_t*>(line.data()), line.size()) != line.size()) {
      lastError = Error::StorageError;
      return false;
    }
  }
  output.flush();
  if (!output.close()) {
    lastError = Error::StorageError;
    return false;
  }
  return true;
}
bool Ao3Client::download(const ao3::Work& work) {
  if (!validId(work.id) || work.title.empty()) {
    lastError = Error::Parse;
    return false;
  }
  const auto path = bookPath(work.id);
  if (!Storage.ensureDirectoryExists("/AO3") || !atomic_file::recover(path)) {
    lastError = Error::StorageError;
    return false;
  }
  const auto temporary = path + ".part";
  if (Storage.exists(temporary.c_str()) && !Storage.remove(temporary.c_str())) {
    lastError = Error::StorageError;
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("AO3", temporary, file)) {
    lastError = Error::StorageError;
    return false;
  }
  const auto url = work.download.empty() ? std::string(ao3::ORIGIN) + "/downloads/" + work.id +
                                               "/work.epub?updated_at=" + ao3::encode(work.updated)
                                         : work.download;
  bool ok = request(url, nullptr, &file);
  file.flush();
  if (!file.close()) {
    lastError = Error::StorageError;
    ok = false;
  }
  if (ok) {
    ZipFile zip(temporary);
    size_t size = 0;
    // Reject a login/challenge/error page before touching a valid local EPUB.
    if (!zip.getInflatedFileSize("META-INF/container.xml", &size) || size == 0 || size > 65536) {
      lastError = Error::Parse;
      ok = false;
    }
    zip.close();
  }
  if (ok && (!invalidateDerivedCache(path) || !atomic_file::publish(path, temporary))) {
    lastError = Error::StorageError;
    ok = false;
  }
  if (!ok) {
    Storage.remove(temporary.c_str());
    return false;
  }
  if (!saveWork(work) || !saveSession()) {
    lastError = Error::StorageError;
    return false;
  }
  return true;
}
