#include "Ao3WebServer.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_https_server.h>
#include <esp_random.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "html/Ao3PageHtml.generated.h"
#include "html/js/ao3Js.generated.h"

namespace {
int randomBytes(void*, unsigned char* output, size_t size) {
  esp_fill_random(output, size);
  return 0;
}
std::string hex(const uint8_t* data, size_t size) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(size * 2);
  for (size_t i = 0; i < size; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 15];
  }
  return out;
}
struct Identity {
  mbedtls_pk_context key;
  mbedtls_x509write_cert cert;
  Identity() {
    mbedtls_pk_init(&key);
    mbedtls_x509write_crt_init(&cert);
  }
  ~Identity() {
    mbedtls_x509write_crt_free(&cert);
    mbedtls_pk_free(&key);
  }
};
struct Body {
  std::unique_ptr<char[]> bytes;
  size_t size = 0;
  ~Body() {
    if (bytes) mbedtls_platform_zeroize(bytes.get(), size + 1);
  }
};
std::string header(httpd_req_t* request, const char* name, size_t limit) {
  const size_t length = httpd_req_get_hdr_value_len(request, name);
  if (!length || length > limit) return {};
  std::string value(length + 1, '\0');
  if (httpd_req_get_hdr_value_str(request, name, value.data(), value.size()) != ESP_OK) return {};
  value.resize(length);
  return value;
}
esp_err_t json(httpd_req_t* request, const char* status, const JsonDocument& doc) {
  std::string response;
  serializeJson(doc, response);
  httpd_resp_set_status(request, status);
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, response.data(), response.size());
}
esp_err_t error(httpd_req_t* request, const char* status, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  return json(request, status, doc);
}
const char* errorName(Ao3Client::Error error) {
  switch (error) {
    case Ao3Client::Error::None:
      return "";
    case Ao3Client::Error::SignIn:
      return "Sign in again. Check your AO3 account in a browser if login is challenged.";
    case Ao3Client::Error::RateLimit:
      return "AO3 asked us to slow down. Try again later.";
    case Ao3Client::Error::Missing:
      return "A work is unavailable to this account.";
    case Ao3Client::Error::StorageError:
      return "SD card error. Check free space and retry.";
    case Ao3Client::Error::Cancelled:
      return "Cancelled. Completed downloads were kept.";
    case Ao3Client::Error::Memory:
      return "Not enough memory. Close other transfers and try again.";
    case Ao3Client::Error::Clock:
      return "Could not set the clock for secure AO3 access.";
    case Ao3Client::Error::Timeout:
      return "AO3 took too long to respond. Try again later.";
    case Ao3Client::Error::Server:
      return "AO3 returned a server error. Try again later.";
    case Ao3Client::Error::Parse:
      return "AO3 returned an unexpected response or exceeded a page limit.";
    case Ao3Client::Error::BridgeConfig:
      return "Check the bridge URL and device token for this Wi-Fi network.";
    default:
      return "Could not securely connect to AO3. Check Wi-Fi and try again.";
  }
}
}  // namespace

Ao3WebServer::~Ao3WebServer() {
  stop();
  mbedtls_platform_zeroize(privateKey.data(), privateKey.size());
}
bool Ao3WebServer::createIdentity() {
  auto identity = makeUniqueNoThrow<Identity>();
  if (!identity) return false;
  auto& key = identity->key;
  auto& cert = identity->cert;
  if (mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY)) != 0 ||
      mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(key), randomBytes, nullptr) != 0)
    return false;
  mbedtls_x509write_crt_set_version(&cert, MBEDTLS_X509_CRT_VERSION_3);
  mbedtls_x509write_crt_set_md_alg(&cert, MBEDTLS_MD_SHA256);
  mbedtls_x509write_crt_set_subject_key(&cert, &key);
  mbedtls_x509write_crt_set_issuer_key(&cert, &key);
  uint8_t serial[16];
  esp_fill_random(serial, sizeof(serial));
  serial[0] &= 0x7f;
  serial[0] |= 1;
  if (mbedtls_x509write_crt_set_serial_raw(&cert, serial, sizeof(serial)) != 0 ||
      mbedtls_x509write_crt_set_subject_name(&cert, "CN=BuddyPoint AO3") != 0 ||
      mbedtls_x509write_crt_set_issuer_name(&cert, "CN=BuddyPoint AO3") != 0 ||
      mbedtls_x509write_crt_set_validity(&cert, "20240101000000", "20491231235959") != 0 ||
      mbedtls_x509write_crt_set_basic_constraints(&cert, 0, -1) != 0 ||
      mbedtls_x509write_crt_set_key_usage(&cert, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0)
    return false;
  // SubjectAltName: the device's current IPv4 address and its local hostname.
  uint8_t san[40]{};
  const char* hostname = "haakanpoint.local";
  const size_t length = std::strlen(hostname);
  san[0] = 0x30;
  san[1] = length + 8;
  san[2] = 0x82;
  san[3] = length;
  std::memcpy(san + 4, hostname, length);
  san[length + 4] = 0x87;
  san[length + 5] = 4;
  const auto ip = WiFi.localIP();
  for (size_t i = 0; i < 4; ++i) san[length + 6 + i] = ip[i];
  if (mbedtls_x509write_crt_set_extension(&cert, MBEDTLS_OID_SUBJECT_ALT_NAME,
                                          MBEDTLS_OID_SIZE(MBEDTLS_OID_SUBJECT_ALT_NAME), 0, san, length + 10) != 0)
    return false;
  const int size = mbedtls_x509write_crt_der(&cert, certificate.data(), certificate.size(), randomBytes, nullptr);
  if (size <= 0 || mbedtls_pk_write_key_pem(&key, privateKey.data(), privateKey.size()) != 0) return false;
  certificateLength = size;
  std::memmove(certificate.data(), certificate.data() + certificate.size() - size, size);
  uint8_t digest[32];
  if (mbedtls_sha256(certificate.data(), certificateLength, digest, 0) != 0) return false;
  certificateFingerprint = hex(digest, sizeof(digest));
  return true;
}
bool Ao3WebServer::begin(const std::string& ip) {
  if (server) return true;
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("AO3WEB", "Cannot start: Wi-Fi disconnected");
    return false;
  }
  if (ESP.getFreeHeap() < 70000 || ESP.getMaxAllocHeap() < 22000) {
    LOG_ERR("AO3WEB", "Cannot start: free heap %u, largest block %u (need 70000 / 22000)", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    return false;
  }
  address = ip;
  if (!createIdentity()) {
    LOG_ERR("AO3WEB", "Could not create local TLS identity");
    return false;
  }
  char code[9];
  std::snprintf(code, sizeof(code), "%08lu", static_cast<unsigned long>(esp_random() % 100000000));
  pairingCode = code;
  uint8_t secret[32];
  esp_fill_random(secret, sizeof(secret));
  access.reset(pairingCode, hex(secret, sizeof(secret)));
  mbedtls_platform_zeroize(secret, sizeof(secret));
  return startListener();
}

bool Ao3WebServer::startListener() {
  // One browser TLS session (~40 KiB per the SDK), plus the HTTPS task. AO3
  // requests run with this listener stopped, including its task and TLS buffers.
  auto config = makeUniqueNoThrow<httpd_ssl_config_t>();
  if (!config) {
    LOG_ERR("AO3WEB", "Cannot allocate HTTPS server configuration");
    return false;
  }
  *config = HTTPD_SSL_CONFIG_DEFAULT();
  config->httpd.max_open_sockets = 1;
  config->httpd.max_uri_handlers = 7;
  config->httpd.max_resp_headers = 10;
  config->httpd.max_req_hdr_len = 2048;
  config->httpd.recv_wait_timeout = 5;
  config->httpd.send_wait_timeout = 5;
  config->port_secure = 8443;
  config->transport_mode = HTTPD_SSL_TRANSPORT_SECURE;
  config->servercert = certificate.data();
  config->servercert_len = certificateLength;
  config->prvtkey_pem = privateKey.data();
  config->prvtkey_len = std::strlen(reinterpret_cast<char*>(privateKey.data())) + 1;
  const auto startResult = httpd_ssl_start(&server, config.get());
  if (startResult != ESP_OK) {
    LOG_ERR("AO3WEB", "HTTPS server startup failed: %s", esp_err_to_name(startResult));
    server = nullptr;
    return false;
  }
  for (const auto* path : {"/", "/ao3.js", "/api/status", "/api/pair", "/api/login", "/api/action", "/api/bridge"}) {
    httpd_uri_t route{};
    route.uri = path;
    route.method = std::strncmp(path, "/api/", 5) == 0 && std::strcmp(path, "/api/status") ? HTTP_POST : HTTP_GET;
    route.handler = handle;
    route.user_ctx = this;
    const auto routeResult = httpd_register_uri_handler(server, &route);
    if (routeResult != ESP_OK) {
      LOG_ERR("AO3WEB", "HTTPS route registration failed: %s", esp_err_to_name(routeResult));
      stop();
      return false;
    }
  }
  return true;
}
void Ao3WebServer::stop() {
  jobs.cancel();
  stopListener();
}
void Ao3WebServer::stopListener() {
  if (server) {
    httpd_ssl_stop(server);
    server = nullptr;
  }
}
void Ao3WebServer::tick() {
  if (listenerFailed || !jobs.busy() || jobs.loginPending() || handlingRequest.load() ||
      static_cast<long>(millis() - startNotBefore.load()) < 0)
    return;
  // Stop waits for the HTTPS task and all incoming sockets to close. Keep the
  // identity and pairing token in RAM so the browser can reconnect unchanged.
  stopListener();
  jobs.tick();
  if (!startListener()) {
    listenerFailed = true;
    jobs.cancel();
    jobs.tick();
    return;
  }
  // Give polling/cancel requests a window between works. Their replies also
  // defer the next job tick until the response has been sent.
  startNotBefore = millis() + 3000;
}
bool Ao3WebServer::takeLogin(std::string& user, std::string& password) {
  if (!jobs.loginPending() || handlingRequest.load() || static_cast<long>(millis() - startNotBefore.load()) < 0)
    return false;
  // Finish sending the acknowledgement and release incoming TLS before handing
  // credentials to the device activity. The panel is not restarted after this.
  LOG_INF("AO3WEB", "Closing panel for device sign-in");
  stopListener();
  return jobs.takeLogin(user, password);
}
esp_err_t Ao3WebServer::handle(httpd_req_t* request) {
  auto* self = static_cast<Ao3WebServer*>(request->user_ctx);
  self->handlingRequest = true;
  httpd_resp_set_hdr(request, "Cache-Control", "no-store");
  httpd_resp_set_hdr(request, "Connection", "close");
  httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
  httpd_resp_set_hdr(request, "Content-Security-Policy",
                     "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; "
                     "form-action 'none'");
  const auto result = self->route(request);
  self->startNotBefore = millis() + 250;
  // Allow the HTTPS task to release its incoming TLS buffers before the queued job.
  httpd_sess_trigger_close(request->handle, httpd_req_to_sockfd(request));
  self->handlingRequest = false;
  return result;
}
esp_err_t Ao3WebServer::route(httpd_req_t* request) {
  const auto host = header(request, "Host", 80);
  const auto origin = request->method == HTTP_POST ? header(request, "Origin", 96) : "https://" + host;
  if (!ao3::WebAccess::sameOrigin(host, origin, address))
    return error(request, "403 Forbidden", "Open the panel directly at the address shown on the reader.");
  const std::string_view path(request->uri);
  if (path == "/" || path == "/ao3.js") {
    httpd_resp_set_hdr(request, "Content-Encoding", "gzip");
    httpd_resp_set_type(request, path == "/" ? "text/html; charset=utf-8" : "application/javascript");
    return path == "/" ? httpd_resp_send(request, Ao3PageHtml, sizeof(Ao3PageHtml))
                       : httpd_resp_send(request, ao3Js, sizeof(ao3Js));
  }
  if (path != "/api/pair") {
    const auto token = header(request, "Authorization", 80);
    std::lock_guard<std::mutex> lock(accessMutex);
    if (!token.starts_with("Bearer ") || !access.authorized(std::string_view(token).substr(7)))
      return error(request, "401 Unauthorized", "Enter the pairing code shown on the reader.");
  }
  if (path == "/api/status") {
    const auto status = jobs.status();
    JsonDocument doc;
    doc["signedIn"] = status.signedIn;
    doc["account"] = status.account;
    doc["busy"] = status.busy;
    doc["phase"] = status.phase;
    doc["title"] = status.title;
    doc["checked"] = status.checked;
    doc["saved"] = status.saved;
    doc["unchanged"] = status.unchanged;
    doc["failed"] = status.failed;
    doc["bytes"] = status.bytes;
    doc["total"] = status.total;
    doc["error"] = errorName(status.error);
    return json(request, "200 OK", doc);
  }
  if (request->content_len == 0 || request->content_len > 4096)
    return error(request, "413 Content Too Large", "Invalid request size.");
  Body body;
  body.size = request->content_len;
  body.bytes = makeUniqueNoThrow<char[]>(body.size + 1);
  if (!body.bytes) return error(request, "503 Service Unavailable", "Not enough memory.");
  size_t received = 0;
  while (received < body.size) {
    const int n = httpd_req_recv(request, body.bytes.get() + received, body.size - received);
    if (n <= 0) return error(request, "408 Request Timeout", "Request interrupted.");
    received += n;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body.bytes.get(), body.size)) return error(request, "400 Bad Request", "Invalid request.");
  if (path == "/api/pair") {
    std::lock_guard<std::mutex> lock(accessMutex);
    if (!access.pair(doc["code"] | ""))
      return error(
          request, access.locked() ? "429 Too Many Requests" : "403 Forbidden",
          access.locked() ? "Pairing locked. Leave AO3 mode on the reader and reopen it." : "Incorrect pairing code.");
    doc.clear();
    doc["token"] = access.token();
    Ao3BridgeConfig config;
    config.load();
    doc["bridge"]["enabled"] = config.enabled;
    doc["bridge"]["default_url"] = config.defaultUrl;
    doc["bridge"]["has_token"] = !config.token.empty();
    doc["bridge"]["ssid"] = WiFi.SSID().c_str();
    auto networks = doc["bridge"]["networks"].to<JsonArray>();
    for (const auto& [ssid, url] : config.networks) {
      auto entry = networks.add<JsonObject>();
      entry["ssid"] = ssid;
      entry["url"] = url;
    }
    return json(request, "200 OK", doc);
  }
  if (path == "/api/bridge") {
    Ao3BridgeConfig config;
    config.load();
    config.enabled = doc["enabled"] | false;
    config.defaultUrl = doc["default_url"] | "";
    while (!config.defaultUrl.empty() && config.defaultUrl.back() == '/') config.defaultUrl.pop_back();
    if (!doc["token"].isNull()) config.token = doc["token"].as<std::string>();
    const std::string ssid = doc["ssid"] | "";
    if (!ssid.empty() && !config.setNetwork(ssid, doc["url"] | ""))
      return error(request, "400 Bad Request", "Invalid Wi-Fi override (maximum eight networks).");
    if (!Ao3BridgeConfig::validUrl(config.defaultUrl) || !Ao3BridgeConfig::validToken(config.token))
      return error(request, "400 Bad Request", "Enter an HTTP(S) API URL and a valid bridge token.");
    if (!config.save()) return error(request, "500 Internal Server Error", "Could not save bridge settings to SD.");
    doc.clear();
    doc["saved"] = true;
    return json(request, "200 OK", doc);
  }
  if (path == "/api/login") {
    std::string user = doc["username"] | "", password = doc["password"] | "";
    doc["password"] = "";
    if (!jobs.enqueue(Ao3WebJobs::Job::Login, std::move(user), std::move(password)))
      return error(request, "409 Conflict", "Another job is running, or the username/password is invalid.");
  } else {
    const std::string action = doc["action"] | "";
    if (action == "cancel")
      jobs.cancel();
    else {
      const auto job = action == "bookmarks" ? Ao3WebJobs::Job::Bookmarks
                       : action == "updates" ? Ao3WebJobs::Job::Updates
                       : action == "forget"  ? Ao3WebJobs::Job::Forget
                                             : Ao3WebJobs::Job::None;
      if (job == Ao3WebJobs::Job::None) return error(request, "400 Bad Request", "Unknown action.");
      if (!jobs.enqueue(job)) return error(request, "409 Conflict", "Another AO3 job is running.");
    }
  }
  doc.clear();
  doc["accepted"] = true;
  if (path == "/api/login") doc["continueOnReader"] = true;
  return json(request, "202 Accepted", doc);
}
