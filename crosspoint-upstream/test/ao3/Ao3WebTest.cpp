#include <Ao3WebAccess.h>
#include <WebPathPolicy.h>
#include <gtest/gtest.h>

TEST(Ao3WebAccess, PairingHasFiveAttemptsAndResetRevokesOldToken) {
  ao3::WebAccess access;
  EXPECT_FALSE(access.authorized(""));
  EXPECT_FALSE(access.pair(""));
  access.reset("00112233", "first-secret");
  EXPECT_FALSE(access.pair("112233"));
  EXPECT_TRUE(access.pair("00112233"));
  EXPECT_TRUE(access.authorized("first-secret"));
  EXPECT_FALSE(access.authorized("first-secreT"));
  for (int i = 0; i < 4; ++i) EXPECT_FALSE(access.pair("wrong"));
  EXPECT_TRUE(access.locked());
  EXPECT_FALSE(access.pair("00112233"));
  access.reset("99887766", "second-secret");
  EXPECT_FALSE(access.locked());
  EXPECT_FALSE(access.authorized("first-secret"));
  EXPECT_TRUE(access.pair("99887766"));
}
TEST(Ao3WebAccess, RejectsCrossOriginAndReboundHosts) {
  using ao3::WebAccess;
  EXPECT_TRUE(WebAccess::sameOrigin("192.168.1.7:8443", "https://192.168.1.7:8443", "192.168.1.7"));
  EXPECT_TRUE(WebAccess::sameOrigin("haakanpoint.local:8443", "https://haakanpoint.local:8443", "192.168.1.7"));
  for (const auto* origin :
       {"", "null", "http://192.168.1.7:8443", "https://elsewhere.test", "https://192.168.1.7:8443.evil"})
    EXPECT_FALSE(WebAccess::sameOrigin("192.168.1.7:8443", origin, "192.168.1.7"));
  EXPECT_FALSE(WebAccess::sameOrigin("attacker.test:8443", "https://attacker.test:8443", "192.168.1.7"));
  EXPECT_FALSE(WebAccess::sameOrigin("192.168.1.8:8443", "https://192.168.1.8:8443", "192.168.1.7"));
}
TEST(Ao3WebPath, RejectsPrivateAncestorsAndFatAliasesButAllowsBooks) {
  ao3test::reset();
  for (const auto* path :
       {"/.crosspoint/ao3-session.json", "/.CROSSPOINT/ao3-session.json.part", "/books/.private/file",
        "/System Volume Information/a", "/xtcache/a", "/books/../secret", "/books/./fic", "/books\\secret", "relative"})
    EXPECT_TRUE(web_path::protectedPath(path)) << path;
  EXPECT_FALSE(web_path::protectedPath("/"));
  EXPECT_FALSE(web_path::protectedPath("/AO3/123.epub"));
  EXPECT_FALSE(web_path::protectedPath("/books/normal.epub"));
  ao3test::directories.insert("/.crosspoint");
  ao3test::aliases["/CROSSP~1"] = "/.crosspoint";
  EXPECT_TRUE(web_path::protectedPath("/CROSSP~1/ao3-session.json"));
  ao3test::files["/books/.secret"] = {};
  ao3test::aliases["/books/SECRET~1"] = "/books/.secret";
  EXPECT_TRUE(web_path::protectedPath("/books/SECRET~1"));
}
