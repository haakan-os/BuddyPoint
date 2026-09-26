#pragma once

#include <string_view>

#include "LibraryIndexFile.h"

namespace library {
// Writes positions in the chosen index order into caller-owned, bounded storage.
// On an I/O error no partial shelf is exposed.
bool filterShelf(LibraryIndexFile& index, SortOrder order, bool markdown, std::string_view foldedQuery, uint16_t* rows,
                 uint16_t capacity, uint16_t& count, bool allTypes = false);
int shelfPositionFor(uint16_t indexRow, const uint16_t* rows, uint16_t count);
// Pinned entries are handled by the caller. Overlaps are positions in this shelf.
int shelfRowFor(int entry, int pinned, const uint16_t* overlaps, uint8_t overlapCount, const uint16_t* rows,
                uint16_t count);
}  // namespace library
