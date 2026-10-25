#pragma once

#include <Ao3WebAccess.h>

#include <array>
#include <memory>
#include <mutex>

#include "Ao3WebJobs.h"

struct httpd_req;

class Ao3WebServer {
 public:
  Ao3WebServer() = default;
  ~Ao3WebServer();
  bool begin(const std::string& ip);
  void stop();
  void tick();
  void cancel() { jobs.cancel(); }
  bool busy() const { return jobs.busy(); }
  Ao3WebJobs::Status status() const { return jobs.status(); }
  void setObserver(void (*callback)(void*), void* context) { jobs.setObserver(callback, context); }
  const std::string& code() const { return pairingCode; }
  const std::string& fingerprint() const { return certificateFingerprint; }

 private:
  void* server = nullptr;
  std::atomic<unsigned long> startNotBefore{0};
  std::atomic<bool> handlingRequest{false};
  // Heap-owned with this server; no persistent private key is written to SD.
  std::array<uint8_t, 2048> certificate{};
  std::array<uint8_t, 512> privateKey{};
  size_t certificateLength = 0;
  std::string address, pairingCode, certificateFingerprint;
  ao3::WebAccess access;
  std::mutex accessMutex;
  Ao3WebJobs jobs;
  bool createIdentity();
  static int handle(httpd_req* request);
  int route(httpd_req* request);
};
