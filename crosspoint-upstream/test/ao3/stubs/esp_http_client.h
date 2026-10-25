#pragma once
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_HTTP_EAGAIN = 0x7007;
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
  bool chunked = false, stallBody = false;
  unsigned headerWaits = 0, readWaits = 0, readAdvanceMs = 0;
  size_t chunkSize = 1024;
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
  bool nonBlocking = false;
  size_t bytesRead = 0, readCalls = 0, headerCalls = 0;
  int readTimeout = -1;
  int transmitBuffer = 0;
};
inline std::deque<Response> responses;
inline std::vector<Request> requests;
inline unsigned long clock = 100;
inline unsigned freeHeap = 150000;
struct Http {
  esp_http_client_config_t config;
  Response response;
  size_t requestIndex, position = 0;
  int socket = -1, peer = -1;
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
  request.transmitBuffer = config->buffer_size_tx;
  ao3test::requests.push_back(std::move(request));
  auto* http = new ao3test::Http{*config, std::move(ao3test::responses.front()), ao3test::requests.size() - 1};
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0) {
    http->socket = sockets[0];
    http->peer = sockets[1];
  }
  ao3test::responses.pop_front();
  return http;
}
inline int esp_http_client_cleanup(esp_http_client_handle_t h) {
  if (h->socket >= 0) close(h->socket);
  if (h->peer >= 0) close(h->peer);
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
inline int esp_http_client_open(esp_http_client_handle_t h, int) {
  const auto& request = ao3test::requests[h->requestIndex];
  // Match the SDK's buffer contract: it batches complete header lines and
  // requires the request line to fit before sending any headers.
  const auto path = request.url.find('/', request.url.find("://") + 3);
  const size_t requestLine = (path == std::string::npos ? 1 : request.url.size() - path) + 16;
  if (requestLine >= static_cast<size_t>(request.transmitBuffer)) return -1;
  for (const auto& [key, value] : request.headers) {
    if (key.size() + value.size() + 7 > static_cast<size_t>(request.transmitBuffer)) return -1;
  }
  return h->response.connect ? ESP_OK : -1;
}
inline int esp_http_client_get_and_clear_last_tls_error(esp_http_client_handle_t, int* error, int* flags) {
  *error = 0;
  *flags = 0;
  return ESP_OK;
}
inline int esp_http_client_write(esp_http_client_handle_t h, const char* bytes, int length) {
  ao3test::requests[h->requestIndex].body.append(bytes, length);
  return length;
}
inline int64_t esp_http_client_fetch_headers(esp_http_client_handle_t h) {
  ++ao3test::requests[h->requestIndex].headerCalls;
  if (h->response.headerWaits) {
    --h->response.headerWaits;
    ao3test::clock += h->config.timeout_ms;
    return -ESP_ERR_HTTP_EAGAIN;
  }
  for (auto& [name, value] : h->response.headers) {
    esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, h->config.user_data, name.data(), value.data()};
    h->config.event_handler(&event);
  }
  if (!h->response.headersComplete) return -1;
  return h->response.chunked ? 0 : static_cast<int64_t>(h->response.body.size());
}
inline int esp_http_client_get_socket(esp_http_client_handle_t h) { return h->socket; }
inline int esp_http_client_set_timeout_ms(esp_http_client_handle_t h, int timeout) {
  h->config.timeout_ms = timeout;
  return ESP_OK;
}
inline int esp_http_client_get_status_code(esp_http_client_handle_t h) { return h->response.status; }
inline int esp_http_client_read(esp_http_client_handle_t h, char* out, int length) {
  auto& request = ao3test::requests[h->requestIndex];
  ++request.readCalls;
  request.readTimeout = h->config.timeout_ms;
  request.nonBlocking = (fcntl(h->socket, F_GETFL, 0) & O_NONBLOCK) != 0;
  if (h->response.stallBody || h->response.readWaits) {
    if (h->response.readWaits) --h->response.readWaits;
    return -ESP_ERR_HTTP_EAGAIN;
  }
  ao3test::clock += h->response.readAdvanceMs;
  size_t size = std::min({static_cast<size_t>(length), h->response.body.size() - h->position, h->response.chunkSize});
  std::memcpy(out, h->response.body.data() + h->position, size);
  h->position += size;
  request.bytesRead += size;
  return static_cast<int>(size);
}
inline bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) {
  return h->response.complete && h->position == h->response.body.size();
}
inline unsigned long millis() { return ao3test::clock; }
inline void delay(unsigned ms) { ao3test::clock += ms; }
inline void configTime(long, int, const char*, const char*) {}
struct FakeEsp {
  unsigned getFreeHeap() const { return ao3test::freeHeap; }
  unsigned getMaxAllocHeap() const { return ao3test::freeHeap; }
};
inline FakeEsp ESP;
