#include <gtest/gtest.h>

#include "Ao3.h"
#include "Ao3Badge.h"

namespace {
bool feed(ao3::Parser& p, const std::string& html, size_t chunk = 1) {
  for (size_t i = 0; i < html.size(); i += chunk) {
    if (!p.feed(reinterpret_cast<const uint8_t*>(html.data() + i), std::min(chunk, html.size() - i))) return false;
  }
  return p.finish();
}
const std::string workHtml = R"(
<!DOCTYPE html><html><body>
<a href="/users/logout">Log Out</a>
<h2 class="title heading">A &amp; B &#x1F426;</h2>
<h3 class="byline heading"><a rel="author" href="/users/Writer/pseuds/Writer">Writer</a>, <a rel="author">Coauthor</a></h3>
<dl class="work meta group"><dd class="rating tags"><ul><li>Teen And Up Audiences</li></ul></dd>
<dd class="fandom tags"><a>Some Fandom</a></dd>
<dd class="published">2025-01-01</dd><dd class="status">2026-10-25</dd>
<dd class="chapters"><a href="/works/123/navigate">3</a>/5</dd><dd class="words">10,234</dd></dl>
<div class="summary module"><blockquote class="userstuff"><p>A summary.</p><p>Second paragraph.</p></blockquote></div>
<ul class="download"><li><a href="/downloads/123/A.epub?updated_at=123&amp;view_adult=true">EPUB</a></li></ul>
</body></html>)";
const std::string listing = R"(
<html><body><a href='/users/logout'>Log Out</a>
<ol class='bookmark index group'>
<li id='bookmark_88' class='bookmark blurb group'><h4 class='heading'>
<a href='/works/123'>First fic</a> by <a rel='author'>Writer</a></h4>
<ul class='tags'><li><a>Fandom</a></li></ul><dl><dd class='chapters'><a>2</a>/?</dd><dd class='words'>1,200</dd></dl>
<blockquote class='userstuff summary'>Private bookmark fic summary</blockquote></li>
<li class='bookmark blurb'><h4 class='heading'><a href='/series/888'>A series (unsupported)</a></h4></li>
<li class='bookmark blurb'><h4 class='heading'><a href='/external_works/9'>External work</a></h4></li>
<li class='bookmark blurb'><h4 class='heading'><a href='/works/456'>Second fic</a> by <a rel='author'>Other</a></h4><dd class='chapters'>1/1</dd></li>
</ol><ol class='pagination actions'><li class='next'><a rel='next' href='/users/Reader/bookmarks?page=2'>Next</a></li></ol>
</body></html>)";
}  // namespace

TEST(Ao3, WorkMetadataAcrossEveryChunkBoundary) {
  for (size_t chunk : {1U, 2U, 7U, 64U, 1024U}) {
    ao3::Parser p(ao3::Parser::Mode::Work, "123");
    ASSERT_TRUE(feed(p, workHtml, chunk));
    EXPECT_EQ(p.work.id, "123");
    EXPECT_EQ(p.work.title, "A & B 🐦");
    EXPECT_EQ(p.work.author, "Writer, Coauthor");
    EXPECT_EQ(p.work.chapters, 3U);
    EXPECT_EQ(p.work.totalChapters, 5U);
    EXPECT_FALSE(p.work.complete());
    EXPECT_EQ(p.work.words, 10234U);
    EXPECT_EQ(p.work.updated, "2026-10-25");
    EXPECT_EQ(p.work.summary, "A summary. Second paragraph.");
    EXPECT_EQ(p.work.fandom, "Some Fandom");
    EXPECT_TRUE(p.signedIn);
    EXPECT_EQ(p.work.download, "https://archiveofourown.org/downloads/123/A.epub?updated_at=123&view_adult=true");
  }
}
TEST(Ao3, BookmarkPaginationSkipsSeriesAndExternalWorks) {
  ao3::Parser p(ao3::Parser::Mode::Listing);
  ASSERT_TRUE(feed(p, listing));
  ASSERT_EQ(p.works.size(), 2U);
  EXPECT_EQ(p.works[0].id, "123");
  EXPECT_EQ(p.works[0].title, "First fic");
  EXPECT_EQ(p.works[0].author, "Writer");
  EXPECT_EQ(p.works[0].totalChapters, 0U);
  EXPECT_TRUE(p.works[1].complete());
  EXPECT_EQ(p.next, "/users/Reader/bookmarks?page=2");
  EXPECT_TRUE(p.validListing);
  EXPECT_TRUE(p.signedIn);
}
TEST(Ao3, LoginFormUsesHiddenCsrfAndRecognizesSignOutForm) {
  ao3::Parser p(ao3::Parser::Mode::Auth);
  ASSERT_TRUE(feed(
      p,
      R"(<input value="a&amp;b==" type="hidden" name="authenticity_token"><input type="password" name="user[password]"><form method="post" action="/users/logout"></form>)"));
  EXPECT_EQ(p.csrf, "a&b==");
  EXPECT_TRUE(p.loginForm);
  EXPECT_TRUE(p.signedIn);
}
TEST(Ao3, PublicBookmarkPageIsNotAnAuthenticatedSession) {
  ao3::Parser p(ao3::Parser::Mode::Listing);
  ASSERT_TRUE(feed(p, "<ol class='bookmark index group'></ol>"));
  EXPECT_TRUE(p.validListing);
  EXPECT_FALSE(p.signedIn);
  EXPECT_TRUE(p.works.empty());
}
TEST(Ao3, LoginAndChallengePagesAreNotWorks) {
  ao3::Parser p(ao3::Parser::Mode::Work, "123");
  EXPECT_FALSE(feed(p, "<h2>Log in</h2><input name='user[password]'>"));
  EXPECT_TRUE(p.loginForm);
  ao3::Parser challenge(ao3::Parser::Mode::Work, "123");
  EXPECT_FALSE(feed(challenge, "<html><h1>Checking your browser</h1></html>"));
}
TEST(Ao3, TruncatedResponseIsNotAccepted) {
  ao3::Parser p(ao3::Parser::Mode::Listing);
  EXPECT_FALSE(feed(p, "<li class='work blurb'><h4 class='heading'><a href='/works/123'>Hello</a></h4>"));
}
TEST(Ao3, BoundsLargeTagsAndResponses) {
  ao3::Parser p(ao3::Parser::Mode::Auth);
  EXPECT_FALSE(feed(p, "<a x='" + std::string(3000, 'a') + "'>"));
  ao3::Parser huge(ao3::Parser::Mode::Auth);
  EXPECT_FALSE(feed(huge, std::string(2 * 1024 * 1024 + 1, 'a'), 4096));
}
TEST(Ao3, BoundsTitlesAndSummary) {
  ao3::Parser p(ao3::Parser::Mode::Work, "123");
  ASSERT_TRUE(feed(p,
                   "<h2 class='title'>" + std::string(5000, 'x') +
                       "</h2><dd class='chapters'>1/1</dd><blockquote class='userstuff summary'>" +
                       std::string(5000, 's') + "</blockquote>",
                   1024));
  EXPECT_EQ(p.work.title.size(), 192U);
  EXPECT_EQ(p.work.summary.size(), 768U);
}
TEST(Ao3, PageOverflowIsExplicitInsteadOfSilentlyDroppingBookmarks) {
  ao3::Parser p(ao3::Parser::Mode::Listing);
  std::string page;
  for (unsigned i = 1; i < 22; ++i)
    page += "<li class='work blurb'><h4 class='heading'><a href='/works/" + std::to_string(i) + "'>Fic</a></h4></li>";
  EXPECT_FALSE(feed(p, page, 37));
  EXPECT_TRUE(p.overflow);
  EXPECT_EQ(p.works.size(), ao3::PAGE_SIZE);
}
TEST(Ao3, ValidatesWorkLinksWithoutAcceptingOtherHosts) {
  std::string id;
  EXPECT_TRUE(ao3::workId(" 12345 ", id));
  EXPECT_EQ(id, "12345");
  EXPECT_TRUE(ao3::workId("https://archiveofourown.org/works/12345/chapters/987?view_adult=true", id));
  EXPECT_TRUE(ao3::workId("/works/12345#main", id));
  for (const auto& bad :
       {"0", "-123", "123evil", "https://evil.test/works/123", "https://archiveofourown.org.evil.test/works/123",
        "https://archiveofourown.org@evil.test/works/123", "http://archiveofourown.org/works/123", "/works/../123",
        "/series/123"})
    EXPECT_FALSE(ao3::workId(bad, id)) << bad;
}
TEST(Ao3, RedirectsCannotLeakSessionToAnotherOriginOrDowngradeTls) {
  std::string result;
  EXPECT_TRUE(ao3::resolveUrl("https://archiveofourown.org/users/login", "/users/Reader", result));
  EXPECT_TRUE(ao3::resolveUrl("https://archiveofourown.org/works/1?page=1", "?page=2", result));
  EXPECT_EQ(result, "https://archiveofourown.org/works/1?page=2");
  for (const auto& target :
       {"//evil.test/path", "http://archiveofourown.org/path", "https://archiveofourown.org:444/path",
        "https://archiveofourown.org.evil/path", "/x\r\nCookie: stolen", "https://user@archiveofourown.org/path"})
    EXPECT_FALSE(ao3::resolveUrl(ao3::ORIGIN, target, result)) << target;
}
TEST(Ao3, CookieRotationRetainsOtherCookiesAndOmitsAttributes) {
  ao3::Cookies c;
  ASSERT_TRUE(c.update("_session=old; Path=/; Secure; HttpOnly"));
  ASSERT_TRUE(c.update("user_credentials=remember; Domain=.archiveofourown.org; Path=/"));
  ASSERT_TRUE(c.update("_session=new; SameSite=Lax"));
  EXPECT_EQ(c.header(), "user_credentials=remember; _session=new");
  ASSERT_TRUE(c.update("user_credentials=; Max-Age=0; Path=/"));
  EXPECT_EQ(c.header(), "_session=new");
}
TEST(Ao3, RejectsMalformedAndOversizedCookieHeaders) {
  ao3::Cookies c;
  ASSERT_TRUE(c.restore("_session=valid"));
  EXPECT_FALSE(c.restore("_session=x\r\nInjected: yes"));
  EXPECT_FALSE(c.restore(std::string(ao3::MAX_COOKIE_BYTES + 1, 'x')));
  EXPECT_FALSE(c.update("_session=bad; Domain=evil.test"));
  EXPECT_FALSE(c.update("_session=bad; Domain=archiveofourown.org.evil.test"));
  EXPECT_FALSE(c.update("_session=bad\r\nX=evil"));
  EXPECT_EQ(c.header(), "_session=valid");
}
TEST(Ao3, UpdateComparisonIncludesNewChaptersSameDayEditsAndCompletion) {
  ao3::Work saved;
  saved.id = "123";
  saved.updated = "2026-10-25";
  saved.chapters = 1;
  saved.words = 100;
  auto remote = saved;
  EXPECT_FALSE(ao3::changed(saved, remote));
  remote.chapters = 2;
  EXPECT_TRUE(ao3::changed(saved, remote));
  remote = saved;
  remote.words = 150;
  EXPECT_TRUE(ao3::changed(saved, remote));
  remote = saved;
  remote.totalChapters = 1;
  EXPECT_TRUE(ao3::changed(saved, remote));
  remote = saved;
  remote.updated = "2026-10-26";
  EXPECT_TRUE(ao3::changed(saved, remote));
  remote.id = "999";
  EXPECT_FALSE(ao3::changed(saved, remote));
}
TEST(Ao3, FormEncodingPreservesCredentialsAndUnicode) {
  EXPECT_EQ(ao3::encode("user+name@example.org&= #"), "user%2Bname%40example.org%26%3D%20%23");
  EXPECT_EQ(ao3::encode("é"), "%C3%A9");
}
TEST(Ao3, CommentsAndScriptsCannotSpoofAuthenticationOrMetadata) {
  ao3::Parser p(ao3::Parser::Mode::Auth);
  ASSERT_TRUE(feed(p,
                   "<!-- > <a href='/users/logout'>Fake</a> --> <script>const link=\"<a "
                   "href='/users/logout'>Fake</a>\";</script><style>/* <a href='/users/logout'> */</style>"));
  EXPECT_FALSE(p.signedIn);
}
TEST(Ao3, AuthorsNotesDoNotReplaceTheSummary) {
  ao3::Parser p(ao3::Parser::Mode::Work, "123");
  ASSERT_TRUE(
      feed(p, workHtml + "<div class='notes module'><blockquote class='userstuff'>Not the summary</blockquote></div>"));
  EXPECT_EQ(p.work.summary, "A summary. Second paragraph.");
}
TEST(Ao3, BoundedTitlesDoNotSplitUnicodeCharacters) {
  ao3::Parser p(ao3::Parser::Mode::Work, "123");
  ASSERT_TRUE(feed(p, "<h2 class='title'>" + std::string(191, 'x') + "🐦</h2><dd class='chapters'>1/1</dd>"));
  EXPECT_EQ(p.work.title, std::string(191, 'x'));
}
TEST(Ao3, RawScriptComparisonsDoNotBreakThePageParser) {
  ao3::Parser p(ao3::Parser::Mode::Work, "123");
  ASSERT_TRUE(feed(p, "<script>if (count < 5) { text = '<fake'; }</script>" + workHtml));
  EXPECT_EQ(p.work.title, "A & B 🐦");
}
TEST(Ao3, BookmarkNotesCannotSpoofAnAuthenticatedPage) {
  ao3::Parser p(ao3::Parser::Mode::Listing);
  ASSERT_TRUE(feed(
      p,
      "<ol class='bookmark index'><li class='bookmark blurb'><h4 class='heading'><a "
      "href='/works/123'>Fic</a></h4><blockquote><a href='/users/logout'>Fake sign out</a></blockquote></li></ol>"));
  EXPECT_FALSE(p.signedIn);
}
TEST(Ao3, ExtractsWarningAndTagMetadataFromWorkAndListing) {
  ao3::Parser work(ao3::Parser::Mode::Work, "123");
  ASSERT_TRUE(feed(
      work, workHtml + "<dd class='warning tags'><ul><li>Major Character Death</li><li>Graphic Depictions Of "
                       "Violence</li></ul></dd><dd class='freeform tags'><ul><li>Fluff</li><li>Angst</li></ul></dd>"));
  EXPECT_EQ(ao3::warningSymbol(work.work.warnings), '!');
  EXPECT_EQ(work.work.tags, "Fluff Angst");
  ao3::Parser list(ao3::Parser::Mode::Listing);
  ASSERT_TRUE(
      feed(list,
           "<li class='work blurb'><h4 class='heading'><a href='/works/123'>Fic</a></h4><ul "
           "class='required-tags'><li><span class='rating'><span>Explicit</span></span></li><li><span "
           "class='warnings'><span>No Archive Warnings Apply</span></span></li></ul><h5 class='fandoms "
           "heading'>Fandom</h5><ul><li class='freeforms'>Fluff</li><li class='freeforms'>Angst</li></ul></li>"));
  ASSERT_EQ(list.works.size(), 1U);
  EXPECT_EQ(ao3::ratingSymbol(list.works[0].rating), 'E');
  EXPECT_EQ(ao3::warningSymbol(list.works[0].warnings), '-');
  EXPECT_EQ(list.works[0].tags, "Fluff · Angst");
  EXPECT_EQ(list.works[0].fandom, "Fandom");
}
TEST(Ao3, WarningSymbolsNeverTreatMissingOrUndisclosedWarningsAsSafe) {
  EXPECT_EQ(ao3::warningSymbol(""), '?');
  EXPECT_EQ(ao3::warningSymbol("Unrecognized warning"), '?');
  EXPECT_EQ(ao3::warningSymbol("Creator Chose Not To Use Archive Warnings"), 'B');
  EXPECT_EQ(ao3::warningSymbol("No Archive Warnings Apply"), '-');
  EXPECT_EQ(ao3::warningSymbol("No Archive Warnings Apply Major Character Death"), '!');
  EXPECT_EQ(ao3::ratingSymbol("Not Rated"), '-');
}
TEST(Ao3, LibraryRecognizesOnlyManagedDownloadPaths) {
  std::string id;
  ASSERT_TRUE(ao3::downloadedId("/AO3/123.epub", id));
  EXPECT_EQ(id, "123");
  for (const auto* path :
       {"/Other/123.epub", "/AO3/123.epub.part", "/AO3/../123.epub", "/AO3/sub/123.epub", "/AO3/.epub", "/AO3/0.epub"})
    EXPECT_FALSE(ao3::downloadedId(path, id)) << path;
}
TEST(Ao3, BadgeDistinguishesAllReadingStatesAndUnknownCompletion) {
  ao3::Work work;
  work.rating = "General Audiences";
  work.warnings = "No Archive Warnings Apply";
  ao3::Badge badge;
  std::vector<decltype(badge.pixels)> masks;
  for (const auto status : {ao3::ReadingStatus::Unread, ao3::ReadingStatus::Reading, ao3::ReadingStatus::Finished,
                            ao3::ReadingStatus::Waiting, ao3::ReadingStatus::NewChapters}) {
    work.readingStatus = status;
    badge.build(work);
    for (const auto& prior : masks) EXPECT_NE(badge.pixels, prior);
    masks.push_back(badge.pixels);
  }
  const auto unknown = badge.pixels;
  work.chapters = 1;
  badge.build(work);
  const auto unfinished = badge.pixels;
  EXPECT_NE(unknown, unfinished);
  work.totalChapters = 1;
  badge.build(work);
  EXPECT_NE(badge.pixels, unfinished);
  EXPECT_NE(badge.pixels, unknown);
}
