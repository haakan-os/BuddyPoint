#pragma once
#include <limits>
#include <vector>
#define makeUniqueNoThrow uncheckedMakeUniqueNoThrow
#include "../../../lib/Memory/Memory.h"
#undef makeUniqueNoThrow
namespace ziptest {
inline size_t maxBuffer = std::numeric_limits<size_t>::max();
inline bool failSecondLargeBuffer = false;
inline std::vector<size_t> allocationSizes;
}  // namespace ziptest
template <typename T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  ziptest::allocationSizes.push_back(count);
  if (count > ziptest::maxBuffer ||
      (ziptest::failSecondLargeBuffer && count > 512 && ziptest::allocationSizes.size() % 2 == 0))
    return nullptr;
  return uncheckedMakeUniqueNoThrow<T>(count);
}
