#include <gtest/gtest.h>

#include <cstdlib>
#include <new>

#include "Ao3.h"

namespace {
thread_local bool denyAllocations = false;
thread_local size_t deniedAllocations = 0;
struct NoAllocationScope {
  NoAllocationScope() {
    deniedAllocations = 0;
    denyAllocations = true;
  }
  ~NoAllocationScope() { denyAllocations = false; }
};
}  // namespace

// Fault injection covers allocations inside std::string, as on the crashed device.
void* operator new(std::size_t size) {
  if (denyAllocations) {
    ++deniedAllocations;
    throw std::bad_alloc();
  }
  if (auto* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return ::operator new(size, std::nothrow); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

TEST(Ao3Cookies, RotationAndExpiryDoNotAllocateDuringTls) {
  ao3::Cookies cookies;
  ASSERT_TRUE(cookies.restore("_session=old; remember=keep"));
  bool rotated = false, expired = false, threw = false;
  {
    NoAllocationScope guard;
    try {
      rotated = cookies.update("_session=new; Domain=.ARCHIVEOFOUROWN.ORG; Secure; HttpOnly");
      expired = cookies.update("remember=; Max-Age=0; Path=/");
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }
  EXPECT_FALSE(threw);
  EXPECT_EQ(deniedAllocations, 0U);
  EXPECT_TRUE(rotated);
  EXPECT_TRUE(expired);
  EXPECT_EQ(cookies.header(), "_session=new");
}

TEST(Ao3Cookies, FullSizeRotationAndOversizeRejectionDoNotAllocate) {
  ao3::Cookies cookies;
  const std::string oldCookie = "s=" + std::string(ao3::MAX_COOKIE_BYTES - 2, 'x');
  const std::string replacement = "s=" + std::string(ao3::MAX_COOKIE_BYTES - 2, 'y');
  const std::string tooLarge = replacement + "z";
  ASSERT_TRUE(cookies.restore(oldCookie));
  bool rotated = false, rejectedExtra = false, rejectedSize = false, threw = false;
  {
    NoAllocationScope guard;
    try {
      rotated = cookies.update(replacement);
      rejectedExtra = !cookies.update("other=value; Secure");
      rejectedSize = !cookies.update(tooLarge);
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }
  EXPECT_FALSE(threw);
  EXPECT_EQ(deniedAllocations, 0U);
  EXPECT_TRUE(rotated);
  EXPECT_TRUE(rejectedExtra);
  EXPECT_TRUE(rejectedSize);
  EXPECT_EQ(cookies.header(), replacement);
  EXPECT_EQ(cookies.header().data()[cookies.header().size()], '\0');
}
TEST(Ao3Cookies, UnavailableStorageReturnsFailureWithoutThrowing) {
  ao3::Cookies cookies;
  bool prepared = true, threw = false;
  {
    NoAllocationScope guard;
    try {
      prepared = cookies.prepare();
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }
  EXPECT_FALSE(threw);
  EXPECT_FALSE(prepared);
  EXPECT_EQ(deniedAllocations, 1U);
  EXPECT_TRUE(cookies.header().empty());
}
TEST(Ao3Cookies, ClearReusesPreparedStorageDuringTls) {
  ao3::Cookies cookies;
  ASSERT_TRUE(cookies.restore("s=old"));
  bool updated = false, threw = false;
  {
    NoAllocationScope guard;
    try {
      cookies.clear();
      updated = cookies.update("s=new");
    } catch (const std::bad_alloc&) {
      threw = true;
    }
  }
  EXPECT_FALSE(threw);
  EXPECT_EQ(deniedAllocations, 0U);
  EXPECT_TRUE(updated);
  EXPECT_EQ(cookies.header(), "s=new");
}
TEST(Ao3Cookies, CanonicalRestoreAndDuplicateReplacementPreserveOtherCookies) {
  ao3::Cookies cookies;
  ASSERT_TRUE(cookies.restore("  a=old;a=duplicate;  keep=yes  "));
  EXPECT_EQ(cookies.header(), "a=old; a=duplicate; keep=yes");
  ASSERT_TRUE(cookies.update("a=new; Domain=archiveofourown.org"));
  EXPECT_EQ(cookies.header(), "keep=yes; a=new");
  ASSERT_TRUE(cookies.update("a=expired; Expires=Thu, 01 Jan 1970 00:00:00 GMT"));
  EXPECT_EQ(cookies.header(), "keep=yes");
  EXPECT_FALSE(cookies.update("keep=no; Domain=evil.test; Domain=archiveofourown.org"));
  EXPECT_EQ(cookies.header(), "keep=yes");
  ASSERT_TRUE(cookies.restore(cookies.header()));
}
