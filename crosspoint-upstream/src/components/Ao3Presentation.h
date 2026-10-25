#pragma once
#include <Ao3.h>
#include <Ao3Badge.h>
#include <FreeInkUICore.h>
#include <I18n.h>

#include <algorithm>

namespace ao3ui {
inline uint8_t summaryLines(int height, int titleLine, int subtitleLine, int padding) {
  // Keep one complete row reachable in landscape and with larger UI fonts.
  const int lines = (height - titleLine * 2 - padding * 2 - 12) / std::max(1, subtitleLine);
  return static_cast<uint8_t>(std::clamp(lines, 1, 6));
}
inline freeink::ui::BitmapRef bitmap(const ao3::Badge& badge) {
  return {badge.pixels.data(), ao3::Badge::SIZE, ao3::Badge::SIZE, freeink::ui::BitmapFormat::BW1, false};
}
inline const char* statusText(ao3::ReadingStatus status) {
  switch (status) {
    case ao3::ReadingStatus::Reading:
      return tr(STR_AO3_READING);
    case ao3::ReadingStatus::Finished:
      return tr(STR_AO3_FINISHED);
    case ao3::ReadingStatus::Waiting:
      return tr(STR_AO3_WAITING);
    case ao3::ReadingStatus::NewChapters:
      return tr(STR_AO3_NEW_CHAPTERS);
    default:
      return tr(STR_AO3_UNREAD);
  }
}
// Built while populating a bounded page/window; the list keeps stable pointers.
inline std::string subtitle(const ao3::Work& work, bool expanded) {
  std::string text = work.author;
  text += "\n" + std::string(tr(STR_AO3_CHAPTERS)) + ": " + std::to_string(work.chapters) + "/" +
          (work.totalChapters ? std::to_string(work.totalChapters) : "?") + " · " + std::to_string(work.words) + " " +
          tr(STR_AO3_WORDS);
  if (expanded) {
    if (!work.fandom.empty()) text += "\n" + work.fandom;
    if (!work.tags.empty()) text += "\n" + work.tags;
    if (!work.summary.empty()) text += "\n" + work.summary;
  }
  return text;
}
}  // namespace ao3ui
