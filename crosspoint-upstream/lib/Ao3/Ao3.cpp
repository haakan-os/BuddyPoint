#include "Ao3.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace ao3 {
namespace {
std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}
bool digits(std::string_view s) {
  return !s.empty() && s.size() <= 12 && s.front() != '0' &&
         std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}
std::string lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}
std::string attr(std::string_view tag, std::string_view name) {
  size_t p = tag.find_first_of(" \t\r\n");
  while (p != std::string_view::npos && p < tag.size()) {
    while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    const auto start = p;
    while (p < tag.size() && tag[p] != '=' && !std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    auto key = tag.substr(start, p - start);
    while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    if (p == tag.size()) break;
    if (tag[p] != '=') continue;
    ++p;
    while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    if (p == tag.size()) break;
    const char q = tag[p] == '\'' || tag[p] == '"' ? tag[p++] : 0;
    const auto begin = p;
    while (p < tag.size() && (q ? tag[p] != q : !std::isspace(static_cast<unsigned char>(tag[p])))) ++p;
    if (lower(key) == name) return decodeText(tag.substr(begin, p - begin));
    if (q && p < tag.size()) ++p;
  }
  return {};
}
bool has(std::string_view tokens, std::string_view token) {
  while (!tokens.empty()) {
    tokens = trim(tokens);
    const auto end = tokens.find_first_of(" \t\r\n");
    if (tokens.substr(0, end) == token) return true;
    if (end == std::string_view::npos) break;
    tokens.remove_prefix(end + 1);
  }
  return false;
}
uint32_t number(std::string_view text) {
  uint64_t value = 0;
  for (char c : text) {
    if (c == ',' || std::isspace(static_cast<unsigned char>(c))) continue;
    if (c < '0' || c > '9') return 0;
    value = value * 10 + c - '0';
    if (value > std::numeric_limits<uint32_t>::max()) return 0;
  }
  return static_cast<uint32_t>(value);
}
void appendCodepoint(std::string& s, uint32_t c) {
  if (!c || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return;
  if (c < 0x80)
    s += static_cast<char>(c);
  else if (c < 0x800) {
    s += static_cast<char>(0xc0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 63));
  } else if (c < 0x10000) {
    s += static_cast<char>(0xe0 | (c >> 12));
    s += static_cast<char>(0x80 | ((c >> 6) & 63));
    s += static_cast<char>(0x80 | (c & 63));
  } else {
    s += static_cast<char>(0xf0 | (c >> 18));
    s += static_cast<char>(0x80 | ((c >> 12) & 63));
    s += static_cast<char>(0x80 | ((c >> 6) & 63));
    s += static_cast<char>(0x80 | (c & 63));
  }
}
bool cookiePart(std::string_view part) {
  auto equal = part.find('=');
  if (equal == 0 || equal == std::string_view::npos) return false;
  for (char c : part)
    if (static_cast<unsigned char>(c) < 33 || c == 127 || c == ';' || c == '\\' || c == '"') return false;
  return true;
}
std::string boundedText(std::string_view text, size_t limit) {
  size_t end = std::min(text.size(), limit);
  // Drop a partial UTF-8 character when the byte budget splits one.
  if (end < text.size()) {
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
  }
  return std::string(text.substr(0, end));
}
}  // namespace

bool trustedUrl(std::string_view url) {
  constexpr std::string_view origin = "https://archiveofourown.org";
  if (url.size() > 2048 || url.substr(0, origin.size()) != origin) return false;
  if (url.size() > origin.size() && url[origin.size()] != '/') return false;
  for (unsigned char c : url)
    if (c <= 32 || c == 127 || c == '\\') return false;
  return true;
}
bool resolveUrl(std::string_view base, std::string_view target, std::string& result) {
  if (target.empty() || target.substr(0, 2) == "//") return false;
  if (target.front() == '/')
    result = std::string(ORIGIN) + std::string(target);
  else if (target.front() == '?' && trustedUrl(base))
    result = std::string(base.substr(0, base.find('?'))) + std::string(target);
  else
    result = target;
  return trustedUrl(result);
}
bool workId(std::string_view input, std::string& id) {
  input = trim(input);
  if (digits(input)) {
    id = input;
    return true;
  }
  if (input.substr(0, 1) != "/") {
    if (!trustedUrl(input)) return false;
    input.remove_prefix(std::string_view(ORIGIN).size());
  }
  constexpr std::string_view prefix = "/works/";
  if (input.substr(0, prefix.size()) != prefix) return false;
  input.remove_prefix(prefix.size());
  const auto end = input.find_first_of("/?#");
  auto value = input.substr(0, end);
  if (!digits(value)) return false;
  id = value;
  return true;
}
bool downloadedId(std::string_view path, std::string& id) {
  constexpr std::string_view prefix = "/AO3/", suffix = ".epub";
  if (!path.starts_with(prefix) || !path.ends_with(suffix)) return false;
  const auto value = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
  if (!digits(value)) return false;
  id = value;
  return true;
}
char ratingSymbol(std::string_view rating) {
  if (rating == "General Audiences") return 'G';
  if (rating == "Teen And Up Audiences") return 'T';
  if (rating == "Mature") return 'M';
  if (rating == "Explicit") return 'E';
  return '-';
}
char warningSymbol(std::string_view warnings) {
  if (warnings.empty()) return '?';
  if (warnings.find("Chose Not To Use Archive Warnings") != std::string_view::npos) return 'B';
  if (warnings.find("Graphic Depictions Of Violence") != std::string_view::npos ||
      warnings.find("Major Character Death") != std::string_view::npos ||
      warnings.find("Rape/Non-Con") != std::string_view::npos || warnings.find("Underage") != std::string_view::npos)
    return '!';
  return warnings == "No Archive Warnings Apply" ? '-' : '?';
}
std::string encode(std::string_view value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
      out += static_cast<char>(c);
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}
std::string decodeText(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '&') {
      const auto end = value.find(';', i + 1);
      if (end != std::string_view::npos && end - i < 14) {
        const auto e = value.substr(i + 1, end - i - 1);
        uint32_t cp = 0;
        if (e == "amp")
          cp = '&';
        else if (e == "lt")
          cp = '<';
        else if (e == "gt")
          cp = '>';
        else if (e == "quot")
          cp = '"';
        else if (e == "apos" || e == "#39")
          cp = '\'';
        else if (e == "nbsp")
          cp = ' ';
        else if (!e.empty() && e[0] == '#') {
          size_t p = 1;
          unsigned base = 10;
          if (p < e.size() && (e[p] == 'x' || e[p] == 'X')) {
            base = 16;
            ++p;
          }
          for (; p < e.size(); ++p) {
            char c = static_cast<char>(std::tolower(static_cast<unsigned char>(e[p])));
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 99;
            if (digit >= base || cp > 0x10ffff / base) {
              cp = 0;
              break;
            }
            cp = cp * base + digit;
          }
        }
        if (cp) {
          appendCodepoint(out, cp);
          i = end;
          continue;
        }
      }
    }
    unsigned char c = value[i];
    if (std::isspace(c)) {
      if (!out.empty() && out.back() != ' ') out += ' ';
    } else if (c >= 32 && c != 127)
      out += static_cast<char>(c);
  }
  return std::string(trim(out));
}
bool changed(const Work& downloaded, const Work& remote) {
  return downloaded.id == remote.id &&
         (downloaded.updated != remote.updated || downloaded.chapters != remote.chapters ||
          downloaded.words != remote.words || downloaded.totalChapters != remote.totalChapters ||
          (!downloaded.download.empty() && !remote.download.empty() && downloaded.download != remote.download));
}
bool Cookies::restore(std::string_view header) {
  if (header.size() > MAX_COOKIE_BYTES) return false;
  auto remaining = header;
  while (!remaining.empty()) {
    auto end = remaining.find(';');
    if (!cookiePart(trim(remaining.substr(0, end)))) return false;
    if (end == std::string_view::npos) break;
    remaining.remove_prefix(end + 1);
  }
  value = header;
  return true;
}
bool Cookies::update(std::string_view cookie) {
  auto first = trim(cookie.substr(0, cookie.find(';')));
  if (!cookiePart(first)) return false;
  const auto key = first.substr(0, first.find('='));
  const auto attributes = lower(cookie.substr(first.size()));
  const auto domain = attributes.find("domain=");
  if (domain != std::string::npos) {
    auto host = trim(std::string_view(attributes).substr(domain + 7));
    host = trim(host.substr(0, host.find(';')));
    if (host != "archiveofourown.org" && host != ".archiveofourown.org") return false;
  }
  std::string out;
  out.reserve(MAX_COOKIE_BYTES);
  auto remaining = std::string_view(value);
  while (!remaining.empty()) {
    const auto end = remaining.find(';');
    const auto part = trim(remaining.substr(0, end));
    if (part.substr(0, part.find('=')) != key) {
      if (!out.empty()) out += "; ";
      out += part;
    }
    if (end == std::string_view::npos) break;
    remaining.remove_prefix(end + 1);
  }
  if (first.size() > key.size() + 1 && attributes.find("max-age=0") == std::string::npos &&
      attributes.find("expires=thu, 01 jan 1970") == std::string::npos) {
    if (!out.empty()) out += "; ";
    out += first;
  }
  if (out.size() > MAX_COOKIE_BYTES) return false;
  value = std::move(out);
  return true;
}

Parser::Parser(Mode mode, std::string id) : mode(mode) {
  work.id = std::move(id);
  tag.reserve(2048);
  text.reserve(1024);
  if (mode == Mode::Listing) works.reserve(PAGE_SIZE);
}
void Parser::capture(Field value, const std::string& closing) {
  if (field != Field::None) return;
  field = value;
  fieldTag = closing;
  fieldNesting = 1;
  text.clear();
}
void Parser::flushField() {
  auto value = decodeText(text);
  switch (field) {
    case Field::Title:
      work.title = boundedText(value, 192);
      break;
    case Field::Author:
      if (!work.author.empty()) work.author += ", ";
      work.author += value;
      work.author = boundedText(work.author, 192);
      break;
    case Field::Summary:
      work.summary = boundedText(value, 768);
      break;
    case Field::Fandom:
      work.fandom = boundedText(value, 192);
      break;
    case Field::Rating:
      work.rating = boundedText(value, 80);
      break;
    case Field::Warnings:
      work.warnings = boundedText(value, 384);
      break;
    case Field::Tags:
      if (!work.tags.empty()) work.tags += " · ";
      work.tags += boundedText(value, 192);
      work.tags = boundedText(work.tags, 192);
      break;
    case Field::Date:
      work.updated = value.substr(0, 32);
      break;
    case Field::Chapters: {
      const auto slash = value.find('/');
      work.chapters = number(value.substr(0, slash));
      work.totalChapters = slash == std::string::npos ? 0 : number(value.substr(slash + 1));
      break;
    }
    case Field::Words:
      work.words = number(value);
      break;
    default:
      break;
  }
  field = Field::None;
  text.clear();
}
void Parser::finishRow() {
  if (!work.id.empty() && !work.title.empty() && works.size() < PAGE_SIZE) {
    const bool duplicate = std::any_of(works.begin(), works.end(), [this](const Work& w) { return w.id == work.id; });
    if (!duplicate) works.push_back(std::move(work));
  } else if (!work.id.empty() && works.size() == PAGE_SIZE)
    overflow = true;
  work = Work{};
  row = false;
  field = Field::None;
  headingDepth = 0;
}
void Parser::token() {
  auto raw = trim(tag);
  if (raw.empty() || raw.front() == '!' || raw.front() == '?') return;
  const bool close = raw.front() == '/';
  if (close) raw.remove_prefix(1);
  const std::string name = lower(raw.substr(0, raw.find_first_of(" \t\r\n/")));
  if (!rawElement.empty()) {
    if (close && name == rawElement) rawElement.clear();
    return;
  }
  if (!close && (name == "script" || name == "style")) {
    rawElement = name;
    return;
  }
  if (close) {
    if (field != Field::None && name == fieldTag && --fieldNesting == 0)
      flushField();
    else if (field != Field::None && (name == "p" || name == "li") && text.size() < 1024)
      text += ' ';
    if (name == "h4") headingDepth = 0;
    if (name == "div" && summaryDepth > 0) --summaryDepth;
    if (name == "li") {
      if (nextDepth > 0) --nextDepth;
      if (row && --rowDepth == 0) finishRow();
    }
    return;
  }
  if (field != Field::None && name == fieldTag) ++fieldNesting;
  const std::string classes = attr(raw, "class");
  const std::string href = attr(raw, "href");
  if (name == "div") {
    if (summaryDepth > 0)
      ++summaryDepth;
    else if (has(classes, "summary"))
      summaryDepth = 1;
  }
  if (!row && (href == "/users/logout" || href == "/users/sign_out" || attr(raw, "action") == "/users/logout"))
    signedIn = true;
  if (name == "input" && attr(raw, "name") == "authenticity_token") csrf = attr(raw, "value").substr(0, 512);
  if (name == "input" && attr(raw, "name") == "user[password]") loginForm = true;
  if ((name == "ol" || name == "ul") && has(classes, "index") && (has(classes, "work") || has(classes, "bookmark")))
    validListing = true;
  if (name == "li") {
    if (nextDepth > 0) ++nextDepth;
    if (has(classes, "next")) nextDepth = 1;
    if (row)
      ++rowDepth;
    else if (mode == Mode::Listing && has(classes, "blurb") && (has(classes, "work") || has(classes, "bookmark"))) {
      row = true;
      rowDepth = 1;
      validListing = true;
    }
  }
  if (name == "a" && (!href.empty()) && (has(attr(raw, "rel"), "next") || nextDepth)) next = href.substr(0, 2048);
  if (mode == Mode::Auth || (mode == Mode::Listing && !row)) return;
  if (name == "h4" && has(classes, "heading")) headingDepth = 1;
  if (name == "a" && headingDepth && work.title.empty()) {
    std::string id;
    if (workId(href, id)) {
      work.id = std::move(id);
      capture(Field::Title, name);
    }
  }
  if (name == "h2" && has(classes, "title")) capture(Field::Title, name);
  if (name == "a" && has(attr(raw, "rel"), "author")) capture(Field::Author, name);
  if (name == "blockquote" && has(classes, "userstuff") && (summaryDepth || has(classes, "summary")))
    capture(Field::Summary, name);
  if (name == "span" && has(classes, "rating")) capture(Field::Rating, name);
  if (name == "span" && has(classes, "warnings")) capture(Field::Warnings, name);
  if (name == "h5" && has(classes, "fandoms")) capture(Field::Fandom, name);
  if (name == "li" && has(classes, "freeforms")) capture(Field::Tags, name);
  if (name == "dd") {
    if (has(classes, "chapters"))
      capture(Field::Chapters, name);
    else if (has(classes, "words"))
      capture(Field::Words, name);
    else if (has(classes, "status") || (has(classes, "published") && work.updated.empty()))
      capture(Field::Date, name);
    else if (has(classes, "fandom"))
      capture(Field::Fandom, name);
    else if (has(classes, "rating"))
      capture(Field::Rating, name);
    else if (has(classes, "warning"))
      capture(Field::Warnings, name);
    else if (has(classes, "freeform"))
      capture(Field::Tags, name);
  }
  if (name == "a" && href.find("/downloads/" + work.id + "/") != std::string::npos &&
      href.find(".epub") != std::string::npos) {
    std::string url;
    if (resolveUrl(ORIGIN, href, url)) work.download = std::move(url);
  }
  if (name == "br" && field != Field::None && text.size() < 1024) text += ' ';
}
bool Parser::feed(const uint8_t* data, size_t size) {
  if (bytes + size > 2 * 1024 * 1024) return false;
  bytes += size;
  for (size_t i = 0; i < size; ++i) {
    char c = static_cast<char>(data[i]);
    // Script/style bodies are HTML raw text: '<' comparisons and quotes do
    // not begin ordinary tags. Only the matching closing element matters.
    if (!rawElement.empty()) {
      if (c == '<') {
        inTag = true;
        tag.clear();
      } else if (inTag && c == '>') {
        token();
        inTag = false;
      } else if (inTag && tag.size() < 32)
        tag += c;
      continue;
    }
    if (comment) {
      if (c == '>' && commentDashes >= 2) {
        comment = false;
        inTag = false;
      }
      commentDashes = c == '-' ? std::min(2U, commentDashes + 1) : 0;
      continue;
    }
    if (!inTag) {
      if (c == '<') {
        inTag = true;
        tag.clear();
        quote = 0;
      } else if (field != Field::None && text.size() < 1024)
        text += c;
    } else if (c == '>' && !quote) {
      token();
      inTag = false;
    } else {
      if (c == quote)
        quote = 0;
      else if (!quote && (c == '"' || c == '\''))
        quote = c;
      if (tag.size() >= 2048) return false;
      tag += c;
      if (tag == "!--") {
        comment = true;
        commentDashes = 0;
        tag.clear();
        quote = 0;
      }
    }
  }
  return true;
}
bool Parser::finish() {
  if (inTag || comment || !rawElement.empty() || row || field != Field::None || overflow) return false;
  return mode != Mode::Work || (!work.id.empty() && !work.title.empty() && work.chapters > 0);
}
}  // namespace ao3
