#include "LibraryShelf.h"

#include <FsHelpers.h>

#include <algorithm>
#include <string>

#include "LibraryText.h"

namespace library {
bool filterShelf(LibraryIndexFile& index, const SortOrder order, const bool markdown,
                 const std::string_view foldedQuery, uint16_t* rows, const uint16_t capacity, uint16_t& count,
                 const bool allTypes) {
  count = 0;
  if (capacity < index.bookCount() || (index.bookCount() > 0 && !rows)) return false;
  uint16_t matched = 0;
  std::string name;
  std::string author;
  for (uint16_t row = 0; row < index.bookCount(); ++row) {
    const uint16_t ordinal = index.ordinalForRow(order, row);
    ClixRecord record{};
    if (ordinal == 0xFFFF || !index.readRecord(ordinal, record) || !index.readName(record, name)) return false;
    if (!allTypes && FsHelpers::hasMarkdownExtension(name) != markdown) continue;
    if (foldedQuery.empty() || matchesQuery(std::string_view(record.fold, record.foldLen), foldedQuery)) {
      rows[matched++] = row;
      continue;
    }
    // readAuthor also returns false for a valid empty author (common for notes).
    if (index.readAuthor(record, author) && matchesQuery(fold(author), foldedQuery)) rows[matched++] = row;
    if (index.ioFailed()) return false;
  }
  count = matched;
  return true;
}

int shelfPositionFor(const uint16_t indexRow, const uint16_t* rows, const uint16_t count) {
  if (!rows || count == 0) return -1;
  const auto* found = std::lower_bound(rows, rows + count, indexRow);
  return found != rows + count && *found == indexRow ? static_cast<int>(found - rows) : -1;
}

int shelfRowFor(const int entry, const int pinned, const uint16_t* overlaps, const uint8_t overlapCount,
                const uint16_t* rows, const uint16_t count) {
  if (entry < pinned || !rows) return -1;
  int row = entry - pinned;
  for (uint8_t i = 0; i < overlapCount; ++i) {
    if (overlaps[i] <= row) ++row;
  }
  return row >= 0 && row < count ? rows[row] : -1;
}
}  // namespace library
