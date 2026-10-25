#include "Ao3WebJobs.h"

#include <algorithm>

Ao3WebJobs::Ao3WebJobs() {
  // fetchList transfers the parser's reserved vector only when bookmarks are requested.
  client.loadSession();
  state.signedIn = client.signedIn();
  state.account = client.account();
  client.setProgress(progress, this);
}
Ao3WebJobs::~Ao3WebJobs() {
  std::fill(loginPassword.begin(), loginPassword.end(), '\0');
  if (queue) queue.close();
  if (started && state.job == Job::Updates) Storage.remove(Ao3Client::UPDATE_QUEUE);
}
bool Ao3WebJobs::enqueue(Job job, std::string user, std::string password) {
  std::lock_guard<std::mutex> lock(mutex);
  if (state.busy || job == Job::None ||
      (job == Job::Login && (user.empty() || user.size() > 80 || password.empty() || password.size() > 256 ||
                             user.find('\0') != std::string::npos || password.find('\0') != std::string::npos))) {
    std::fill(password.begin(), password.end(), '\0');
    return false;
  }
  state.job = job;
  state.busy = true;
  state.phase = "Queued";
  state.title.clear();
  state.checked = state.saved = state.unchanged = state.failed = state.bytes = state.total = 0;
  state.error = Ao3Client::Error::None;
  loginUser = std::move(user);
  loginPassword = std::move(password);
  cancelled = false;
  started = false;
  return true;
}
Ao3WebJobs::Status Ao3WebJobs::status() const {
  std::lock_guard<std::mutex> lock(mutex);
  return state;
}
bool Ao3WebJobs::busy() const {
  std::lock_guard<std::mutex> lock(mutex);
  return state.busy;
}
bool Ao3WebJobs::progress(void* context, size_t bytes, size_t total) {
  auto* self = static_cast<Ao3WebJobs*>(context);
  {
    std::lock_guard<std::mutex> lock(self->mutex);
    self->state.bytes = bytes;
    self->state.total = total;
  }
  if (self->observer) self->observer(self->observerContext);
  return !self->cancelled.load();
}
void Ao3WebJobs::finish(Ao3Client::Error error) {
  if (queue) queue.close();
  if (started && state.job == Job::Updates) Storage.remove(Ao3Client::UPDATE_QUEUE);
  std::vector<ao3::Work>().swap(works);
  remote = {};
  prior = {};
  next.clear();
  std::lock_guard<std::mutex> lock(mutex);
  std::fill(loginPassword.begin(), loginPassword.end(), '\0');
  loginPassword.clear();
  loginUser.clear();
  state.busy = false;
  state.error = error;
  state.title.clear();
  state.bytes = state.total = 0;
  state.phase = error == Ao3Client::Error::Cancelled ? "Cancelled"
                : error == Ao3Client::Error::None    ? "Finished"
                                                     : "Stopped";
  state.signedIn = client.signedIn();
  state.account = client.account();
  if (error == Ao3Client::Error::None && state.failed) state.phase = "Finished with errors";
}
void Ao3WebJobs::process(const std::string& id) {
  remote = {};
  prior = {};
  {
    std::lock_guard<std::mutex> lock(mutex);
    state.phase = "Checking fic";
    state.title = id;
  }
  bool ok = client.fetchWork(id, remote);
  bool unchanged = false;
  if (ok) {
    unchanged = client.readDownloaded(id, prior) && !ao3::changed(prior, remote);
    if (!unchanged) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        state.phase = "Downloading";
        state.title = remote.title;
      }
      ok = client.download(remote);
    }
  }
  const auto error = client.error();
  {
    std::lock_guard<std::mutex> lock(mutex);
    ++state.checked;
    if (!ok)
      ++state.failed;
    else if (unchanged)
      ++state.unchanged;
    else
      ++state.saved;
  }
  remote = {};
  prior = {};
  if (!ok && (error == Ao3Client::Error::SignIn || error == Ao3Client::Error::RateLimit ||
              error == Ao3Client::Error::Cancelled || error == Ao3Client::Error::Memory ||
              error == Ao3Client::Error::StorageError))
    finish(error);
}
void Ao3WebJobs::tick() {
  const auto current = status();
  if (!current.busy) return;
  if (cancelled) {
    finish(Ao3Client::Error::Cancelled);
    return;
  }
  if (current.job == Job::Login) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      state.phase = "Signing in";
    }
    const bool ok = client.signIn(loginUser, loginPassword);
    finish(ok ? Ao3Client::Error::None : client.error());
    return;
  }
  if (current.job == Job::Forget) {
    const bool ok = client.signOut();
    finish(ok ? Ao3Client::Error::None : client.error());
    return;
  }
  if (!started) {
    started = true;
    cursor = pages = 0;
    works.clear();
    if (current.job == Job::Bookmarks) {
      if (!client.signedIn()) {
        finish(Ao3Client::Error::SignIn);
        return;
      }
      next = std::string(ao3::ORIGIN) + "/users/" + ao3::encode(client.account()) + "/bookmarks";
    } else if (!client.snapshotDownloads() || !Storage.openFileForRead("AO3", Ao3Client::UPDATE_QUEUE, queue)) {
      finish(client.error() == Ao3Client::Error::None ? Ao3Client::Error::StorageError : client.error());
      return;
    }
  }
  if (current.job == Job::Bookmarks) {
    if (cursor == works.size()) {
      if (next.empty()) {
        finish();
        return;
      }
      if (++pages > 500) {
        finish(Ao3Client::Error::Parse);
        return;
      }
      const auto url = next;
      works.clear();
      cursor = 0;
      {
        std::lock_guard<std::mutex> lock(mutex);
        state.phase = "Loading bookmarks";
      }
      if (!client.fetchList(url, true, works, next)) {
        finish(client.error());
        return;
      }
      // The web sync needs only IDs; release list metadata before the next
      // browser handshake or work request on the constrained heap.
      for (auto& work : works) {
        auto id = std::move(work.id);
        work = {};
        work.id = std::move(id);
      }
      if (next == url) {
        finish(Ao3Client::Error::Parse);
        return;
      }
      return;
    }
    const auto id = works[cursor++].id;
    process(id);
  } else {
    if (!queue.available()) {
      finish();
      return;
    }
    std::string id;
    id.reserve(13);
    while (queue.available()) {
      const int c = queue.read();
      if (c == '\n') break;
      if (id.size() < 13) id += static_cast<char>(c);
    }
    if (client.readDownloaded(id, prior)) process(id);
  }
}
