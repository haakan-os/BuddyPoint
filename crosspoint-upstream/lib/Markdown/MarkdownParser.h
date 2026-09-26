#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace markdown {

// A bounded streaming Markdown subset. Raw HTML is escaped, never executed.
class Parser {
 public:
  using Write = bool (*)(void*, const char*, size_t);
  using Heading = bool (*)(void*, unsigned, std::string_view);
  Parser(Write write, void* context, Heading heading = nullptr, void* headingContext = nullptr)
      : write(write), context(context), heading(heading), headingContext(headingContext) {}
  bool feed(const char* bytes, size_t size);
  bool finish();
  static bool escape(Write write, void* context, std::string_view text);

 private:
  static constexpr size_t LINE_CAPACITY = 2048;
  char line[LINE_CAPACITY]{};
  char pending[LINE_CAPACITY]{};
  size_t length = 0;
  size_t pendingLength = 0;
  bool hasPending = false;
  bool fragment = false;
  bool pendingFragment = false;
  bool pendingEndsLine = true;
  bool firstLine = true;
  bool paragraph = false;
  bool quote = false;
  bool table = false;
  bool indentedCode = false;
  char fence = 0;
  size_t fenceLength = 0;
  char list = 0;
  unsigned headingCount = 0;
  bool ok = true;
  Write write;
  void* context;
  Heading heading;
  void* headingContext;

  bool emit(std::string_view text);
  bool escaped(std::string_view text);
  bool inlineText(std::string_view text, unsigned depth = 0);
  bool acceptLine(std::string_view text, bool continued, bool endsLine = true);
  bool renderLine(std::string_view text, bool continued);
  bool renderHeading(std::string_view text, int level);
  bool renderTableRow(std::string_view text, bool header);
  bool closeBlocks();
  bool codeText(std::string_view text);
};
}  // namespace markdown
