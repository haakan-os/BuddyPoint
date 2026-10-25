#include "Ao3Activity.h"

#include <FontCacheManager.h>
#include <I18n.h>
#include <Memory.h>
#include <WiFi.h>

#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/Ao3Presentation.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;
namespace {
constexpr auto ACTION_CANCEL = static_cast<fui::ActionId>(2);
const char* errorText(Ao3Client::Error error) {
  switch (error) {
    case Ao3Client::Error::SignIn:
      return tr(STR_AO3_AUTH_ERROR);
    case Ao3Client::Error::RateLimit:
      return tr(STR_AO3_RATE_LIMIT);
    case Ao3Client::Error::Missing:
      return tr(STR_AO3_MISSING);
    case Ao3Client::Error::Parse:
      return tr(STR_AO3_PARSE_ERROR);
    case Ao3Client::Error::StorageError:
      return tr(STR_AO3_STORAGE_ERROR);
    case Ao3Client::Error::Cancelled:
      return tr(STR_AO3_CANCELLED);
    case Ao3Client::Error::Memory:
      return tr(STR_AO3_MEMORY_ERROR);
    case Ao3Client::Error::Clock:
      return tr(STR_AO3_CLOCK_ERROR);
    default:
      return tr(STR_AO3_NETWORK_ERROR);
  }
}
std::string chapterLabel(const ao3::Work& work) {
  return std::string(tr(STR_AO3_CHAPTERS)) + ": " + std::to_string(work.chapters) + "/" +
         (work.totalChapters ? std::to_string(work.totalChapters) : "?") + "  " + std::to_string(work.words) + " " +
         tr(STR_AO3_WORDS);
}
}  // namespace

Ao3Activity::Ao3Activity(GfxRenderer& renderer, MappedInputManager& input, std::string user, std::string password)
    : UiListActivity("AO3", renderer, input), loginUser(std::move(user)), loginPassword(std::move(password)) {
  labels.reserve(rows.size());
  subtitles.reserve(ao3::PAGE_SIZE);
  client.setProgress(progress, this);
}
Ao3Activity::~Ao3Activity() { std::fill(loginPassword.begin(), loginPassword.end(), '\0'); }
void Ao3Activity::onEnter() {
  const bool incomingLogin = !loginUser.empty() && !loginPassword.empty();
  if (!incomingLogin) client.loadSession();
  UiListActivity::onEnter();
  app.on(ACTION_CANCEL, cancelAction, this);
  if (incomingLogin) {
    // File Transfer hands over its existing WiFi connection with the credentials.
    wifiStarted = true;
    schedule(Job::Login);
  } else {
    show(View::Menu);
  }
}
void Ao3Activity::onExit() {
  std::fill(loginPassword.begin(), loginPassword.end(), '\0');
  if (wifiStarted) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
  UiListActivity::onExit();
}
bool Ao3Activity::preventAutoSleep() { return view == View::Busy || pending != Job::None; }
bool Ao3Activity::handleHomeGesture() {
  if (view != View::Busy) return false;
  cancelled = homeAfterCancel = true;
  return true;
}
const char* Ao3Activity::headerTitle() const { return tr(STR_AO3_TITLE); }
int Ao3Activity::listCount() const { return view == View::Busy || view == View::Message ? 0 : labels.size(); }
void Ao3Activity::rebuildLabels() {
  labels.clear();
  subtitles.clear();
  if (view == View::Menu) {
    labels.emplace_back(tr(STR_AO3_DOWNLOADS));
    labels.emplace_back(tr(STR_AO3_OPEN_WORK));
    labels.emplace_back(tr(STR_SEARCH));
    labels.emplace_back(tr(STR_AO3_BOOKMARKS));
    labels.emplace_back(tr(STR_AO3_SYNC_BOOKMARKS));
    labels.emplace_back(tr(STR_AO3_CHECK_UPDATES));
    labels.emplace_back(client.signedIn() ? std::string(tr(STR_AO3_SIGN_OUT)) + " (" + client.account() + ")"
                                          : tr(STR_AO3_SIGN_IN));
  } else if (view == View::List) {
    for (size_t i = 0; i < works.size(); ++i) {
      labels.emplace_back(works[i].title);
      subtitles.emplace_back(ao3ui::subtitle(works[i], true));
      badges[i].build(works[i]);
    }
    if (page > 1) labels.emplace_back(tr(STR_PREV_PAGE));
    if (more) labels.emplace_back(tr(STR_NEXT_PAGE));
    if (works.empty()) labels.emplace_back(tr(STR_AO3_NO_WORKS));
  } else if (view == View::Detail) {
    labels.emplace_back(local ? tr(STR_AO3_READ) : tr(STR_DOWNLOAD));
    labels.emplace_back(local ? tr(STR_AO3_CHECK_UPDATE) : tr(STR_BACK));
    labels.emplace_back(selected.title);
    labels.emplace_back(selected.author);
    labels.emplace_back(chapterLabel(selected));
    labels.emplace_back(ao3ui::statusText(selected.readingStatus));
    labels.emplace_back(selected.complete() ? tr(STR_AO3_COMPLETE) : tr(STR_AO3_IN_PROGRESS));
    labels.emplace_back(selected.warnings.empty() ? tr(STR_AO3_WARNINGS_UNKNOWN) : selected.warnings);
    if (!selected.updated.empty()) labels.emplace_back(std::string(tr(STR_AO3_UPDATED)) + ": " + selected.updated);
    if (!selected.rating.empty()) labels.emplace_back(selected.rating);
    if (!selected.fandom.empty()) labels.emplace_back(selected.fandom);
    if (!selected.tags.empty()) labels.emplace_back(selected.tags);
    if (!selected.summary.empty()) labels.emplace_back(selected.summary);
    labels.emplace_back(tr(STR_AO3_BADGE_LEGEND));
    badges[0].build(selected);
  }
}
void Ao3Activity::show(View next) {
  {
    RenderLock lock;
    view = next;
    nav.reset();
    rebuildLabels();
    app.clearTapFlash();
    closeRouting();
  }
  requestUpdate();
}
void Ao3Activity::showMessage(const std::string& text) {
  {
    RenderLock lock;
    message = text;
  }
  show(View::Message);
}
void Ao3Activity::showError() { showMessage(errorText(client.error())); }
void Ao3Activity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
                                      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (view == View::Busy || view == View::Message) {
    auto textStyle = screen.theme().bodyText;
    textStyle.align = fui::TextAlign::Center;
    if (view == View::Busy) {
      screen.target().text(screen.takeBottom(screen.theme().rowHeight), progressText.c_str(), textStyle);
      fui::ButtonProps cancel;
      cancel.label = tr(STR_CANCEL);
      cancel.action = ACTION_CANCEL;
      screen.button(cancel, screen.takeBottom(screen.theme().rowHeight));
    }
    const auto body = screen.contentRect();
    textStyle.maxLines = std::min<int>(12, body.height / screen.target().lineHeight(textStyle.font));
    screen.target().text(body, view == View::Busy ? status.c_str() : message.c_str(), textStyle);
    return;
  }
  for (size_t i = 0; i < labels.size(); ++i) {
    rows[i].label = labels[i].c_str();
    rows[i].actionValue = i;
    rows[i].subtitle = view == View::List && i < subtitles.size() ? subtitles[i].c_str() : nullptr;
    rows[i].icon = view == View::List && i < works.size() ? ao3ui::bitmap(badges[i]) : fui::BitmapRef{};
    if (view == View::Detail && i == 2) rows[i].icon = ao3ui::bitmap(badges[0]);
  }
  fui::ListProps props;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = view == View::Detail ? 4 : 2;
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines =
      ao3ui::summaryLines(screen.contentRect().height, screen.target().lineHeight(props.labelText.font),
                          screen.target().lineHeight(props.subtitleText.font),
                          std::max(screen.theme().listRowPaddingY, screen.theme().listTouchRowPaddingY));
  props.items = rows.data();
  props.count = labels.size();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  screen.list(props);
}
void Ao3Activity::drawFooter() {
  if (view != View::Busy && view != View::Message) {
    UiListActivity::drawFooter();
    return;
  }
  const auto hints = mappedInput.mapLabels(view == View::Busy ? tr(STR_CANCEL) : tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}
void Ao3Activity::cancelAction(const fui::ActionEvent&, void* context) {
  static_cast<Ao3Activity*>(context)->cancelled = true;
}
bool Ao3Activity::progress(void* context, size_t bytes, size_t total) {
  auto* self = static_cast<Ao3Activity*>(context);
  self->mappedInput.update(true);
  if (self->mappedInput.wasReleased(MappedInputManager::Button::Back)) self->cancelled = true;
  if (self->mappedInput.wasHomeGesture()) self->cancelled = self->homeAfterCancel = true;
  self->routeTouch(self->mappedInput);
  if (millis() - self->lastPaint >= 1500) {
    self->lastPaint = millis();
    {
      RenderLock lock;
      self->progressText = total ? std::to_string(static_cast<uint64_t>(bytes) * 100 / total) + "%"
                                 : std::to_string(bytes / 1024) + " KB";
      if (self->processed) self->progressText += " · " + std::to_string(self->processed) + " " + tr(STR_AO3_CHECKED);
    }
    self->requestUpdate();
  }
  return !self->cancelled;
}
void Ao3Activity::schedule(Job job) {
  pending = job;
  cancelled = homeAfterCancel = false;
  {
    RenderLock lock;
    status = job == Job::Login ? tr(STR_AO3_SIGNING_IN) : job == Job::Download ? tr(STR_DOWNLOADING) : tr(STR_LOADING);
    progressText.clear();
  }
  show(View::Busy);
}
bool Ao3Activity::ensureWifi(Job job) {
  if (WiFi.status() == WL_CONNECTED) return true;
  wifiStarted = true;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    LOG_ERR("AO3", "OOM: WiFi activity");
    showMessage(tr(STR_AO3_MEMORY_ERROR));
    return false;
  }
  startActivityForResult(std::move(wifi), [this, job](const ActivityResult& result) {
    if (result.isCancelled) {
      std::fill(loginPassword.begin(), loginPassword.end(), '\0');
      loginPassword.clear();
      show(View::Menu);
    } else
      schedule(job);
  });
  return false;
}
bool Ao3Activity::handleCustomInput() {
  if (pending != Job::None) {
    const auto job = pending;
    pending = Job::None;
    if (!(job == Job::List && source == Source::Downloads) && !ensureWifi(job)) return true;
    requestUpdateAndWait();
    if (auto* cache = renderer.getFontCacheManager()) cache->releaseSdFontCaches();
    execute(job);
    if (homeAfterCancel) onGoHome(HomeMenuItem::APPS);
    return true;
  }
  if (view == View::Message && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    show(View::Menu);
    return true;
  }
  return false;
}
std::string Ao3Activity::listUrl(size_t number) const {
  if (source == Source::Bookmarks)
    return std::string(ao3::ORIGIN) + "/users/" + ao3::encode(client.account()) +
           "/bookmarks?page=" + std::to_string(number);
  return std::string(ao3::ORIGIN) + "/works/search?work_search%5Bquery%5D=" + ao3::encode(query) +
         "&page=" + std::to_string(number);
}
void Ao3Activity::execute(Job job) {
  if (job == Job::List) {
    works.clear();
    if (source == Source::Downloads) {
      if (!client.downloadedPage((page - 1) * ao3::PAGE_SIZE, works, more)) {
        showError();
        return;
      }
    } else {
      std::string next;
      if (!client.fetchList(listUrl(page), source == Source::Bookmarks, works, next)) {
        showError();
        return;
      }
      more = !next.empty();
      for (auto& work : works) {
        ao3::Work saved;
        if (client.readDownloaded(work.id, saved)) work.readingStatus = saved.readingStatus;
      }
    }
    show(View::List);
  } else if (job == Job::Work) {
    const auto id = selected.id;
    works.clear();
    if (!client.fetchWork(id, selected)) {
      showError();
      return;
    }
    ao3::Work prior;
    local = client.readDownloaded(id, prior);
    if (local) selected.readingStatus = prior.readingStatus;
    show(View::Detail);
  } else if (job == Job::Login) {
    const bool ok = client.signIn(loginUser, loginPassword);
    std::fill(loginPassword.begin(), loginPassword.end(), '\0');
    loginPassword.clear();
    LOG_INF("AO3", "Device sign-in finished: success %d, error %u", ok, static_cast<unsigned>(client.error()));
    if (ok)
      showMessage(tr(STR_AO3_SIGNED_IN));
    else
      showError();
  } else if (job == Job::Download || job == Job::UpdateOne) {
    works.clear();
    ao3::Work remote;
    if (!client.fetchWork(selected.id, remote)) {
      showError();
      return;
    }
    ao3::Work previous;
    if (client.readDownloaded(remote.id, previous) && !ao3::changed(previous, remote)) {
      showMessage(tr(STR_AO3_UP_TO_DATE));
      return;
    }
    if (!client.download(remote)) {
      showError();
      return;
    }
    selected = std::move(remote);
    local = true;
    showMessage(tr(STR_AO3_DOWNLOADED));
  } else if (job == Job::Bookmarks || job == Job::Updates)
    sync(job == Job::Bookmarks);
}
void Ao3Activity::activateIndex(int index) {
  if (view == View::Menu) {
    page = 1;
    if (index == 0) {
      source = Source::Downloads;
      schedule(Job::List);
    } else if (index == 1)
      editQuery(true);
    else if (index == 2)
      editQuery(false);
    else if (index == 3 || index == 4) {
      if (!client.signedIn()) {
        showMessage(tr(STR_AO3_AUTH_ERROR));
        return;
      }
      source = Source::Bookmarks;
      schedule(index == 3 ? Job::List : Job::Bookmarks);
    } else if (index == 5)
      schedule(Job::Updates);
    else if (index == 6) {
      if (client.signedIn()) {
        if (client.signOut())
          show(View::Menu);
        else
          showError();
      } else
        editLogin();
    }
  } else if (view == View::List) {
    if (index < static_cast<int>(works.size())) {
      selected = works[index];
      if (source == Source::Downloads) {
        local = true;
        show(View::Detail);
      } else
        schedule(Job::Work);
    } else {
      const int navigation = index - works.size();
      if (page > 1 && navigation == 0)
        --page;
      else if (more)
        ++page;
      else
        return;
      schedule(Job::List);
    }
  } else if (view == View::Detail) {
    if (index == 0) {
      if (local) {
        app.clearTapFlash();
        onSelectBook(Ao3Client::bookPath(selected.id));
      } else
        schedule(Job::Download);
    } else if (index == 1) {
      if (local)
        schedule(Job::UpdateOne);
      else
        onBackButton();
    }
  }
}
void Ao3Activity::onBackButton() {
  if (view == View::Busy) {
    cancelled = true;
    return;
  }
  if (view == View::Menu) {
    onGoHome(HomeMenuItem::APPS);
    return;
  }
  if (view == View::Detail)
    schedule(Job::List);
  else
    show(View::Menu);
}
void Ao3Activity::editQuery(bool link) {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, link ? tr(STR_AO3_OPEN_WORK) : tr(STR_SEARCH), "",
                                               256, link ? InputType::Url : InputType::Text);
  if (!keyboard) {
    LOG_ERR("AO3", "OOM: query keyboard");
    showMessage(tr(STR_AO3_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(keyboard), [this, link](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto& text = std::get<KeyboardResult>(result.data).text;
    if (link) {
      std::string id;
      if (!ao3::workId(text, id)) {
        showMessage(tr(STR_AO3_INVALID_LINK));
        return;
      }
      selected = ao3::Work{};
      selected.id = std::move(id);
      schedule(Job::Work);
    } else {
      query = text;
      source = Source::Search;
      page = 1;
      schedule(Job::List);
    }
  });
}
void Ao3Activity::editLogin() {
  auto keyboard =
      makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_USERNAME), "", 80, InputType::Text);
  if (!keyboard) {
    LOG_ERR("AO3", "OOM: login keyboard");
    showMessage(tr(STR_AO3_MEMORY_ERROR));
    return;
  }
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    loginUser = std::get<KeyboardResult>(result.data).text;
    auto password =
        makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PASSWORD), "", 256, InputType::Password);
    if (!password) {
      LOG_ERR("AO3", "OOM: password keyboard");
      showMessage(tr(STR_AO3_MEMORY_ERROR));
      return;
    }
    startActivityForResult(std::move(password), [this](const ActivityResult& answer) {
      if (answer.isCancelled) return;
      loginPassword = std::get<KeyboardResult>(answer.data).text;
      schedule(Job::Login);
    });
  });
}
void Ao3Activity::sync(bool bookmarks) {
  works.clear();
  processed = downloaded = unchanged = failed = 0;
  HalFile queue;
  if (!bookmarks) {
    if (!client.snapshotDownloads() || !Storage.openFileForRead("AO3", Ao3Client::UPDATE_QUEUE, queue)) {
      showError();
      return;
    }
  }
  std::string next = bookmarks ? listUrl(1) : "";
  bool stop = false;
  size_t pages = 0;
  while (!stop && !cancelled) {
    if (bookmarks) {
      // Bound a changing/malformed pagination chain; never loop indefinitely.
      if (++pages > 500) {
        showMessage(tr(STR_AO3_PAGE_LIMIT));
        return;
      }
      const auto current = next;
      works.clear();
      if (!client.fetchList(current, true, works, next)) {
        ++failed;
        stop = true;
        break;
      }
      if (next == current) {
        ++failed;
        stop = true;
        break;
      }
    } else {
      works.clear();
      works.reserve(ao3::PAGE_SIZE);
      while (queue.available() && works.size() < ao3::PAGE_SIZE) {
        std::string id;
        id.reserve(12);
        while (queue.available()) {
          const int c = queue.read();
          if (c == '\n') break;
          if (id.size() < 13) id += static_cast<char>(c);
        }
        ao3::Work prior;
        if (client.readDownloaded(id, prior)) works.push_back(std::move(prior));
      }
    }
    for (const auto& item : works) {
      if (cancelled) break;
      {
        RenderLock lock;
        status = item.title;
      }
      requestUpdateAndWait();
      ao3::Work remote, prior;
      if (!client.fetchWork(item.id, remote)) {
        ++failed;
        if (client.error() == Ao3Client::Error::RateLimit || client.error() == Ao3Client::Error::SignIn ||
            client.error() == Ao3Client::Error::Cancelled) {
          stop = true;
          break;
        }
      } else if (client.readDownloaded(item.id, prior) && !ao3::changed(prior, remote))
        ++unchanged;
      else if (client.download(remote))
        ++downloaded;
      else {
        ++failed;
        if (client.error() == Ao3Client::Error::RateLimit || client.error() == Ao3Client::Error::SignIn ||
            client.error() == Ao3Client::Error::Cancelled) {
          stop = true;
          break;
        }
      }
      ++processed;
    }
    if (bookmarks ? next.empty() : !queue.available()) break;
  }
  if (queue) queue.close();
  if (!bookmarks) Storage.remove(Ao3Client::UPDATE_QUEUE);
  std::string summary = std::to_string(downloaded) + " " + tr(STR_AO3_SAVED) + ", " + std::to_string(unchanged) + " " +
                        tr(STR_AO3_UNCHANGED) + ", " + std::to_string(failed) + " " + tr(STR_AO3_FAILED);
  if (stop || cancelled) summary += "\n" + std::string(cancelled ? tr(STR_AO3_CANCELLED) : errorText(client.error()));
  processed = 0;
  showMessage(summary);
}
