#include "MarkdownParser.h"

#include <cstdio>
#include <cstring>

namespace markdown {
namespace {
bool space(char c) { return c == ' ' || c == '\t'; }
std::string_view trim(std::string_view text) {
  while (!text.empty() && space(text.front())) text.remove_prefix(1);
  while (!text.empty() && (space(text.back()) || text.back() == '\r')) text.remove_suffix(1);
  return text;
}
size_t run(std::string_view text, char c) {
  size_t n = 0;
  while (n < text.size() && text[n] == c) ++n;
  return n;
}
bool rule(std::string_view text, char marker, size_t minimum) {
  size_t count = 0;
  for (char c : text) {
    if (c == marker)
      ++count;
    else if (!space(c))
      return false;
  }
  return count >= minimum;
}
bool tableSeparator(std::string_view text) {
  if (text.find('|') == std::string_view::npos) return false;
  if (!text.empty() && text.front() == '|') text.remove_prefix(1);
  if (!text.empty() && text.back() == '|') text.remove_suffix(1);
  while (!text.empty()) {
    const size_t pipe = text.find('|');
    auto cell = trim(text.substr(0, pipe));
    if (!cell.empty() && cell.front() == ':') cell.remove_prefix(1);
    if (!cell.empty() && cell.back() == ':') cell.remove_suffix(1);
    if (cell.size() < 3 || run(cell, '-') != cell.size()) return false;
    if (pipe == std::string_view::npos) break;
    text.remove_prefix(pipe + 1);
  }
  return true;
}
size_t closing(std::string_view text, std::string_view marker, size_t start) {
  size_t found = text.find(marker, start);
  while (found != std::string_view::npos && found > 0 && text[found - 1] == '\\')
    found = text.find(marker, found + marker.size());
  return found;
}
}  // namespace

bool Parser::escape(Write write, void* context, std::string_view text) {
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    const char* entity = nullptr;
    switch (text[i]) {
      case '&':
        entity = "&amp;";
        break;
      case '<':
        entity = "&lt;";
        break;
      case '>':
        entity = "&gt;";
        break;
      case '"':
        entity = "&quot;";
        break;
      case '\'':
        entity = "&apos;";
        break;
      default:
        break;
    }
    const unsigned char c = text[i];
    const bool invalid = c < 32 && c != '\t' && c != '\n' && c != '\r';
    if (!entity && !invalid) continue;
    if (i > start && !write(context, text.data() + start, i - start)) return false;
    if (entity && !write(context, entity, std::strlen(entity))) return false;
    start = i + 1;
  }
  return start == text.size() || write(context, text.data() + start, text.size() - start);
}
bool Parser::emit(std::string_view text) {
  if (ok && !text.empty()) ok = write(context, text.data(), text.size());
  return ok;
}
bool Parser::escaped(std::string_view text) {
  if (ok) ok = escape(write, context, text);
  return ok;
}

bool Parser::inlineText(std::string_view text, unsigned depth) {
  if (depth >= 6) return escaped(text);
  size_t plain = 0;
  for (size_t i = 0; i < text.size() && ok;) {
    const char c = text[i];
    if (c == '\\' && i + 1 < text.size()) {
      escaped(text.substr(plain, i - plain));
      escaped(text.substr(i + 1, 1));
      i += 2;
      plain = i;
      continue;
    }
    if (c == '`') {
      const size_t count = run(text.substr(i), '`');
      const size_t end = closing(text, text.substr(i, count), i + count);
      if (end != std::string_view::npos) {
        escaped(text.substr(plain, i - plain));
        emit("<code>");
        escaped(text.substr(i + count, end - i - count));
        emit("</code>");
        i = end + count;
        plain = i;
        continue;
      }
    }
    const bool image = c == '!' && i + 1 < text.size() && text[i + 1] == '[';
    if (c == '[' || image) {
      const size_t labelStart = i + (image ? 2 : 1);
      const size_t labelEnd = closing(text, "]", labelStart);
      if (labelEnd != std::string_view::npos && labelEnd + 1 < text.size() && text[labelEnd + 1] == '(') {
        size_t end = labelEnd + 2;
        int nesting = 1;
        while (end < text.size()) {
          if (text[end] == '\\' && end + 1 < text.size()) {
            end += 2;
            continue;
          }
          if (text[end] == '(') ++nesting;
          if (text[end] == ')' && --nesting == 0) break;
          ++end;
        }
        if (end < text.size()) {
          escaped(text.substr(plain, i - plain));
          // Offline reader: show link labels and image alt text without fetching URLs.
          if (image) emit("[");
          inlineText(text.substr(labelStart, labelEnd - labelStart), depth + 1);
          if (image) emit("]");
          i = end + 1;
          plain = i;
          continue;
        }
      }
    }
    if ((c == '*' || c == '_') && (c != '_' || i == 0 || space(text[i - 1]))) {
      const size_t markers = run(text.substr(i), c);
      const size_t count = markers >= 3 ? 3 : markers;
      size_t end = closing(text, text.substr(i, count), i + count);
      // Let an inner emphasis consume its marker before the outer pair closes.
      if (count == 2 && end != std::string_view::npos && run(text.substr(end), c) == 3 &&
          text.substr(i + count, end - i - count).find(c) != std::string_view::npos)
        ++end;
      if (end != std::string_view::npos && end > i + count && !space(text[i + count]) && !space(text[end - 1])) {
        escaped(text.substr(plain, i - plain));
        emit(count == 3 ? "<strong><em>" : count == 2 ? "<strong>" : "<em>");
        inlineText(text.substr(i + count, end - i - count), depth + 1);
        emit(count == 3 ? "</em></strong>" : count == 2 ? "</strong>" : "</em>");
        i = end + count;
        plain = i;
        continue;
      }
    }
    ++i;
  }
  return escaped(text.substr(plain));
}

bool Parser::codeText(std::string_view text) {
  size_t start = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != ' ' && text[i] != '\t') continue;
    escaped(text.substr(start, i - start));
    emit(text[i] == '\t' ? "&#160;&#160;&#160;&#160;" : "&#160;");
    start = i + 1;
  }
  return escaped(text.substr(start));
}

bool Parser::closeBlocks() {
  if (paragraph) {
    emit("</p>\n");
    paragraph = false;
  }
  if (list) {
    emit(list == 'o' ? "</ol>\n" : "</ul>\n");
    list = 0;
  }
  if (quote) {
    emit("</blockquote>\n");
    quote = false;
  }
  if (table) {
    emit("</tbody></table>\n");
    table = false;
  }
  if (indentedCode) {
    emit("</code></pre></div>\n");
    indentedCode = false;
  }
  return ok;
}
bool Parser::renderHeading(std::string_view text, int level) {
  closeBlocks();
  const unsigned id = ++headingCount;
  char tag[64];
  std::snprintf(tag, sizeof(tag), "<h%d id=\"heading-%u\">", level, id);
  emit(tag);
  inlineText(text);
  std::snprintf(tag, sizeof(tag), "</h%d>\n", level);
  emit(tag);
  if (ok && heading) ok = heading(headingContext, id, text);
  return ok;
}
bool Parser::renderTableRow(std::string_view text, bool header) {
  text = trim(text);
  if (!text.empty() && text.front() == '|') text.remove_prefix(1);
  if (!text.empty() && text.back() == '|') text.remove_suffix(1);
  emit("<tr>");
  while (true) {
    const size_t pipe = closing(text, "|", 0);
    emit(header ? "<th>" : "<td>");
    inlineText(trim(text.substr(0, pipe)));
    emit(header ? "</th>" : "</td>");
    if (pipe == std::string_view::npos) break;
    text.remove_prefix(pipe + 1);
  }
  return emit("</tr>\n");
}

bool Parser::renderLine(std::string_view text, bool continued) {
  if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
  auto trimmed = trim(text);
  if (fence) {
    if (!continued && run(trimmed, fence) >= fenceLength && trim(trimmed.substr(run(trimmed, fence))).empty()) {
      fence = 0;
      return emit("</code></pre></div>\n");
    }
    codeText(text);
    return emit(continued ? "" : "<br/>\n");
  }
  if (continued) {
    if (indentedCode) return codeText(text);
    if (!paragraph) {
      closeBlocks();
      emit("<p>");
      paragraph = true;
    }
    return inlineText(text);
  }
  if (trimmed.empty()) return closeBlocks();
  if ((trimmed.front() == '`' || trimmed.front() == '~') && run(trimmed, trimmed.front()) >= 3) {
    closeBlocks();
    fence = trimmed.front();
    fenceLength = run(trimmed, fence);
    return emit("<div><pre><code>");
  }
  if (!list && (text.substr(0, 4) == "    " || text.front() == '\t')) {
    if (!indentedCode) {
      closeBlocks();
      indentedCode = true;
      emit("<div><pre><code>");
    }
    codeText(text.substr(text.front() == '\t' ? 1 : 4));
    return emit("<br/>\n");
  }
  if (indentedCode) closeBlocks();
  if (table) {
    if (trimmed.find('|') != std::string_view::npos) return renderTableRow(trimmed, false);
    closeBlocks();
  }
  const size_t hashes = run(trimmed, '#');
  if (hashes > 0 && hashes <= 6 && (hashes == trimmed.size() || space(trimmed[hashes]))) {
    auto title = trim(trimmed.substr(hashes));
    const size_t end = title.find_last_not_of('#');
    if (end != std::string_view::npos && end + 1 < title.size() && space(title[end]))
      title = trim(title.substr(0, end));
    return renderHeading(title, static_cast<int>(hashes));
  }
  if (rule(trimmed, '-', 3) || rule(trimmed, '*', 3) || rule(trimmed, '_', 3)) {
    closeBlocks();
    return emit("<hr/>\n");
  }
  if (trimmed.front() == '>') {
    if (!quote) {
      closeBlocks();
      quote = true;
      emit("<blockquote>");
    }
    emit("<p>");
    inlineText(trim(trimmed.substr(1)));
    return emit("</p>\n");
  }
  if (quote) closeBlocks();
  char listType = 0;
  size_t prefix = 0;
  if ((trimmed.front() == '-' || trimmed.front() == '*' || trimmed.front() == '+') && trimmed.size() > 1 &&
      space(trimmed[1])) {
    listType = 'u';
    prefix = 2;
  } else {
    while (prefix < trimmed.size() && trimmed[prefix] >= '0' && trimmed[prefix] <= '9') ++prefix;
    if (prefix > 0 && prefix < 10 && prefix + 1 < trimmed.size() &&
        (trimmed[prefix] == '.' || trimmed[prefix] == ')') && space(trimmed[prefix + 1])) {
      listType = 'o';
      prefix += 2;
    }
  }
  if (listType) {
    if (list != listType) {
      closeBlocks();
      list = listType;
      emit(list == 'o' ? "<ol>" : "<ul>");
    }
    emit("<li>");
    inlineText(trim(trimmed.substr(prefix)));
    return emit("</li>\n");
  }
  if (list) closeBlocks();
  if (!paragraph) {
    emit("<p>");
    paragraph = true;
  }
  const bool hardBreak = text.size() >= 2 && text.substr(text.size() - 2) == "  ";
  inlineText(trimmed);
  return emit(hardBreak ? "<br/>\n" : "\n");
}

bool Parser::acceptLine(std::string_view text, bool continued, bool endsLine) {
  if (firstLine) {
    if (text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    firstLine = false;
  }
  if (hasPending) {
    const auto trimmed = trim(text);
    const auto previous = trim(std::string_view(pending, pendingLength));
    const bool plainLine = !previous.empty() && previous.front() != '#' && previous.front() != '>' &&
                           previous.front() != '-' && previous.front() != '+' && previous.front() != '*' &&
                           previous.front() != '`' && previous.front() != '~' && previous.front() != '_' &&
                           !(previous.front() >= '0' && previous.front() <= '9') &&
                           std::string_view(pending, pendingLength).substr(0, 4) != "    " && pending[0] != '\t';
    if (!fence && !indentedCode && !pendingFragment && !continued && plainLine) {
      if (rule(trimmed, '=', 1) || rule(trimmed, '-', 1)) {
        hasPending = false;
        return renderHeading(trim(std::string_view(pending, pendingLength)), trimmed.front() == '=' ? 1 : 2);
      }
      if (std::string_view(pending, pendingLength).find('|') != std::string_view::npos && tableSeparator(trimmed)) {
        closeBlocks();
        emit("<table><thead>");
        renderTableRow(std::string_view(pending, pendingLength), true);
        emit("</thead><tbody>\n");
        table = true;
        hasPending = false;
        return ok;
      }
    }
    renderLine(std::string_view(pending, pendingLength), pendingFragment);
    if (pendingFragment && pendingEndsLine) emit(fence || indentedCode ? "<br/>\n" : "\n");
  }
  std::memcpy(pending, text.data(), text.size());
  pendingLength = text.size();
  hasPending = true;
  pendingFragment = continued;
  pendingEndsLine = endsLine;
  return ok;
}

bool Parser::feed(const char* bytes, size_t size) {
  for (size_t i = 0; i < size && ok; ++i) {
    if (bytes[i] == '\n') {
      acceptLine(std::string_view(line, length), fragment);
      length = 0;
      fragment = false;
      continue;
    }
    if (length == LINE_CAPACITY) {
      // Preserve the UTF-8 sequence straddling the fixed buffer boundary.
      size_t split = length;
      while (split > 0 && (static_cast<unsigned char>(line[split - 1]) & 0xc0) == 0x80) --split;
      if (split < length || (static_cast<unsigned char>(line[length - 1]) & 0x80)) {
        if (split > 0) --split;
      }
      if (split == 0) split = length;
      acceptLine(std::string_view(line, split), true, false);
      std::memmove(line, line + split, length - split);
      length -= split;
      fragment = true;
    }
    line[length++] = bytes[i];
  }
  return ok;
}
bool Parser::finish() {
  if (length) acceptLine(std::string_view(line, length), fragment);
  if (hasPending) {
    renderLine(std::string_view(pending, pendingLength), pendingFragment);
    if (pendingFragment && pendingEndsLine) emit(fence || indentedCode ? "<br/>\n" : "\n");
  }
  hasPending = false;
  length = 0;
  if (fence) {
    emit("</code></pre></div>\n");
    fence = 0;
  }
  return closeBlocks();
}
}  // namespace markdown
