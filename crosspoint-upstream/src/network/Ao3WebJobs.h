#pragma once

#include <atomic>
#include <mutex>

#include "Ao3Client.h"

// HTTP handlers enqueue only. All account/SD/network work runs on the activity
// task; the HTTPS task can keep serving status and cancellation during TLS I/O.
class Ao3WebJobs {
 public:
  enum class Job { None, Login, Forget, Bookmarks, Updates };
  struct Status {
    Job job = Job::None;
    bool busy = false, signedIn = false;
    std::string account, phase = "Ready", title;
    size_t checked = 0, saved = 0, unchanged = 0, failed = 0, bytes = 0, total = 0;
    Ao3Client::Error error = Ao3Client::Error::None;
  };
  Ao3WebJobs();
  ~Ao3WebJobs();
  bool enqueue(Job job, std::string user = {}, std::string password = {});
  void cancel() { cancelled = true; }
  Status status() const;
  bool busy() const;
  void tick();
  void setObserver(void (*callback)(void*), void* context) {
    observer = callback;
    observerContext = context;
  }

 private:
  Ao3Client client;
  // Reused work metadata lives with the heap-owned server, away from the task stack.
  ao3::Work remote, prior;
  mutable std::mutex mutex;
  Status state;
  std::atomic<bool> cancelled{false};
  std::string loginUser, loginPassword, next;
  std::vector<ao3::Work> works;
  HalFile queue;
  size_t cursor = 0, pages = 0;
  bool started = false;
  void (*observer)(void*) = nullptr;
  void* observerContext = nullptr;
  void finish(Ao3Client::Error error = Ao3Client::Error::None);
  void process(const std::string& id);
  static bool progress(void* context, size_t bytes, size_t total);
};
