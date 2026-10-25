#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ao3 {
constexpr size_t PAGE_SIZE = 20;
constexpr size_t MAX_COOKIE_BYTES = 4096;
constexpr const char* ORIGIN = "https://archiveofourown.org";

enum class ReadingStatus : uint8_t { Unread, Reading, Finished, Waiting, NewChapters };

struct Work {
  std::string id;
  std::string title;
  std::string author;
  std::string summary;
  std::string fandom;
  std::string rating;
  std::string warnings;
  std::string tags;
  std::string updated;
  std::string download;
  uint32_t words = 0;
  uint32_t chapters = 0;
  uint32_t totalChapters = 0;
  ReadingStatus readingStatus = ReadingStatus::Unread;
  bool complete() const { return chapters > 0 && chapters == totalChapters; }
};

bool workId(std::string_view input, std::string& id);
bool downloadedId(std::string_view path, std::string& id);
char ratingSymbol(std::string_view rating);
char warningSymbol(std::string_view warnings);
bool trustedUrl(std::string_view url);
bool resolveUrl(std::string_view base, std::string_view target, std::string& result);
std::string encode(std::string_view value);
std::string decodeText(std::string_view value);
bool changed(const Work& downloaded, const Work& remote);

// Only same-origin requests may use this jar. Set-Cookie attributes are never
// forwarded as cookies, and expired cookies replace/remove their older values.
class Cookies {
 public:
  // Reserve bounded storage before TLS; subsequent updates do not allocate.
  bool prepare();
  bool update(std::string_view setCookie);
  bool restore(std::string_view header);
  std::string_view header() const { return {value ? value.get() : "", length}; }
  void clear();

 private:
  // Lazy allocation keeps offline library clients small and stays off the task stack.
  std::unique_ptr<char[]> value;
  size_t length = 0;
  bool aliases(std::string_view input) const;
};

// Streaming, bounded HTML extraction; no page-sized DOM or response string.
// The parser lives on the heap during a request, not on the firmware task stack.
class Parser {
 public:
  enum class Mode { Work, Listing, Auth };
  explicit Parser(Mode mode, std::string id = "");
  bool feed(const uint8_t* data, size_t size);
  bool finish();
  Work work;
  std::vector<Work> works;
  std::string next;
  std::string csrf;
  bool signedIn = false;
  bool loginForm = false;
  bool validListing = false;
  bool overflow = false;

 private:
  enum class Field { None, Title, Author, Summary, Fandom, Rating, Warnings, Tags, Date, Chapters, Words };
  Mode mode;
  std::string tag;
  std::string text;
  Field field = Field::None;
  std::string fieldTag;
  int fieldNesting = 0;
  int rowDepth = 0;
  int headingDepth = 0;
  int nextDepth = 0;
  int summaryDepth = 0;
  std::string rawElement;
  bool comment = false;
  unsigned commentDashes = 0;
  bool inTag = false;
  bool row = false;
  char quote = 0;
  size_t bytes = 0;
  void token();
  void capture(Field value, const std::string& closing);
  void flushField();
  void finishRow();
};
}  // namespace ao3
