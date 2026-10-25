#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int HTTP_EVENT_ON_HEADER = 1;
constexpr int HTTP_METHOD_POST = 1;
struct esp_http_client_event_t {
  int event_id;
  void* user_data;
  char* header_key;
  char* header_value;
};
struct esp_http_client_config_t {
  const char* url = nullptr;
  int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
  bool disable_auto_redirect = false;
  esp_err_t (*crt_bundle_attach)(void*) = nullptr;
  esp_err_t (*event_handler)(esp_http_client_event_t*) = nullptr;
  void* user_data = nullptr;
};
namespace ao3test {
struct Response {
  int status = 200;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  bool complete = true;
  bool connect = true;
  bool headersComplete = true;
  Response(int status, std::string body, std::vector<std::pair<std::string, std::string>> headers = {},
           bool complete = true, bool connect = true)
      : status(status), body(std::move(body)), headers(std::move(headers)), complete(complete), connect(connect) {}
};
struct Request {
  std::string url, body;
  std::map<std::string, std::string> headers;
  int method = 0;
  bool tlsVerified = false, manualRedirect = false;
  unsigned long started = 0;
};
inline std::deque<Response> responses;
inline std::vector<Request> requests;
inline unsigned long clock = 100;
inline unsigned freeHeap = 150000;
struct Http {
  esp_http_client_config_t config;
  Response response;
  size_t requestIndex, position = 0;
};
}  // namespace ao3test
using esp_http_client_handle_t = ao3test::Http*;
inline auto esp_http_client_init(const esp_http_client_config_t* config) {
  if (ao3test::responses.empty()) return static_cast<esp_http_client_handle_t>(nullptr);
  ao3test::Request request;
  request.url = config->url;
  request.tlsVerified = config->crt_bundle_attach != nullptr;
  request.manualRedirect = config->disable_auto_redirect;
  request.started = ao3test::clock;
  ao3test::requests.push_back(std::move(request));
  auto* http = new ao3test::Http{*config, std::move(ao3test::responses.front()), ao3test::requests.size() - 1};
  ao3test::responses.pop_front();
  return http;
}
inline int esp_http_client_cleanup(esp_http_client_handle_t h) {
  delete h;
  return ESP_OK;
}
inline int esp_http_client_set_header(esp_http_client_handle_t h, const char* k, const char* v) {
  ao3test::requests[h->requestIndex].headers[k] = v;
  return ESP_OK;
}
inline int esp_http_client_set_method(esp_http_client_handle_t h, int method) {
  ao3test::requests[h->requestIndex].method = method;
  return ESP_OK;
}
inline int esp_http_client_open(esp_http_client_handle_t h, int) { return h->response.connect ? ESP_OK : -1; }
inline int esp_http_client_write(esp_http_client_handle_t h, const char* bytes, int length) {
  ao3test::requests[h->requestIndex].body.append(bytes, length);
  return length;
}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
  for (auto& [name, value] : h->response.headers) {
    esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, h->config.user_data, name.data(), value.data()};
    h->config.event_handler(&event);
  }
  return h->response.headersComplete ? static_cast<int64_t>(h->response.body.size()) : -1;
}
inline int esp_http_client_get_status_code(esp_http_client_handle_t h) { return h->response.status; }
inline int esp_http_client_read(esp_http_client_handle_t h, char* out, int length) {
  size_t size = std::min<size_t>(length, h->response.body.size() - h->position);
  std::memcpy(out, h->response.body.data() + h->position, size);
  h->position += size;
  return static_cast<int>(size);
}
inline bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { return h->response.complete; }
inline unsigned long millis() { return ao3test::clock; }
inline void delay(unsigned ms) { ao3test::clock += ms; }
inline void configTime(long, int, const char*, const char*) {}
struct FakeEsp {
  unsigned getFreeHeap() const { return ao3test::freeHeap; }
  unsigned getMaxAllocHeap() const { return ao3test::freeHeap; }
};
inline FakeEsp ESP;
