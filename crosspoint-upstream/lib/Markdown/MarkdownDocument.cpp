#include "MarkdownDocument.h"

#include <BufferedFile.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "MarkdownParser.h"
#include "StoredZipWriter.h"

namespace markdown {
namespace {
constexpr uint32_t CONVERTER_VERSION = 2;
constexpr char CONTAINER[] =
    "<?xml version=\"1.0\"?><container version=\"1.0\" "
    "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"content.opf\" "
    "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
constexpr char XHTML_START[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>";
constexpr char XHTML_BODY[] = "</title></head><body>\n";
constexpr char NCX_START[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
    "version=\"2005-1\"><head><meta name=\"dtb:uid\" content=\"haakanpoint-markdown\"/></head><docTitle><text>";
constexpr char OPF_START[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" "
    "unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier "
    "id=\"id\">haakanpoint-markdown</dc:identifier><dc:title>";
constexpr char OPF_END[] =
    "</dc:title><dc:language>en</dc:language></metadata><manifest><item id=\"text\" href=\"content.xhtml\" "
    "media-type=\"application/xhtml+xml\"/><item id=\"toc\" href=\"toc.ncx\" "
    "media-type=\"application/x-dtbncx+xml\"/></manifest><spine toc=\"toc\"><itemref "
    "idref=\"text\"/></spine></package>";

bool writeText(void* context, const char* data, size_t size) {
  auto& out = *static_cast<serialization::BufferedFileWriter*>(context);
  out.write(data, size);
  return true;  // flush() reports sticky short-write errors before publication.
}
bool writeHeading(void* context, unsigned id, std::string_view title) {
  auto& out = *static_cast<serialization::BufferedFileWriter*>(context);
  char tag[128];
  std::snprintf(tag, sizeof(tag), "<navPoint id=\"heading-%u\" playOrder=\"%u\"><navLabel><text>", id, id + 1);
  out.write(tag, std::strlen(tag));
  Parser::escape(writeText, context, title);
  std::snprintf(tag, sizeof(tag), "</text></navLabel><content src=\"content.xhtml#heading-%u\"/></navPoint>\n", id);
  out.write(tag, std::strlen(tag));
  return true;
}
bool zipWrite(void* context, const uint8_t* bytes, size_t size) {
  return static_cast<HalFile*>(context)->write(bytes, size) == size;
}
bool zipSeek(void* context, uint32_t offset) { return static_cast<HalFile*>(context)->seek(offset); }
bool zipText(void* context, const char* bytes, size_t size) {
  return static_cast<StoredZipWriter*>(context)->append(bytes, size);
}

struct Fingerprint {
  uint64_t hash = 14695981039346656037ULL;
  uint32_t size = 0;
  uint32_t version = CONVERTER_VERSION;
};
static_assert(sizeof(Fingerprint) == 16);

// One transient allocation keeps parsing and SD transfer buffers off the task stack.
struct Builder {
  uint8_t buffer[2048];
  bool fingerprint(HalFile& file, Fingerprint& result) {
    const size_t size = file.size();
    if (size > UINT32_MAX) return false;
    result.size = size;
    size_t remaining = size;
    while (remaining) {
      const size_t count = std::min(remaining, sizeof(buffer));
      if (file.read(buffer, count) != static_cast<int>(count)) return false;
      for (size_t i = 0; i < count; ++i) result.hash = (result.hash ^ buffer[i]) * 1099511628211ULL;
      remaining -= count;
      vTaskDelay(1);
    }
    return file.seek(0);
  }
  // Sidecar v1: magic[8], source size[4], source FNV[8], EPUB size[4], EPUB FNV[8], EPUB.
  // Validate all bytes before selecting it; stale or interrupted syncs use native Markdown.
  bool mathCopy(const std::string& sourcePath, const Fingerprint& source, HalFile& math, uint64_t& hash) {
    const size_t slash = sourcePath.find_last_of('/');
    const std::string_view name(sourcePath.data() + (slash == std::string::npos ? 0 : slash + 1),
                                sourcePath.size() - (slash == std::string::npos ? 0 : slash + 1));
    uint64_t nameHash = 14695981039346656037ULL;
    for (const unsigned char c : name) nameHash = (nameHash ^ c) * 1099511628211ULL;
    char filename[48];
    std::snprintf(filename, sizeof(filename), "buddy-math-%016llx.bmath", static_cast<unsigned long long>(nameHash));
    const std::string path = sourcePath.substr(0, slash == std::string::npos ? 0 : slash + 1) + filename;
    if (!Storage.exists(path.c_str()) || !Storage.openFileForRead("MD", path, math) || math.size() < 36 ||
        math.size() > 8 * 1024 * 1024 || math.read(buffer, 32) != 32 || std::memcmp(buffer, "BUDMATH1", 8) != 0)
      return false;
    auto integer = [this](size_t offset, size_t count) {
      uint64_t value = 0;
      for (size_t i = 0; i < count; ++i) value |= uint64_t(buffer[offset + i]) << (8 * i);
      return value;
    };
    if (integer(8, 4) != source.size || integer(12, 8) != source.hash || integer(20, 4) != math.size() - 32)
      return false;
    const uint64_t expected = integer(24, 8);
    hash = 14695981039346656037ULL;
    size_t remaining = math.size() - 32;
    bool first = true;
    while (remaining) {
      const size_t count = std::min(remaining, sizeof(buffer));
      if (math.read(buffer, count) != static_cast<int>(count)) return false;
      if (first && std::memcmp(buffer, "PK\x03\x04", 4) != 0) return false;
      first = false;
      for (size_t i = 0; i < count; ++i) hash = (hash ^ buffer[i]) * 1099511628211ULL;
      remaining -= count;
      vTaskDelay(1);
    }
    return hash == expected && math.seek(32);
  }
  bool copyMath(HalFile& math, const std::string& base) {
    HalFile output;
    if (!Storage.openFileForWrite("MD", base + ".epub.tmp", output)) return false;
    size_t remaining = math.size() - 32;
    while (remaining) {
      const size_t count = std::min(remaining, sizeof(buffer));
      if (math.read(buffer, count) != static_cast<int>(count) || output.write(buffer, count) != count) return false;
      remaining -= count;
      vTaskDelay(1);
    }
    return output.close();
  }
  bool convert(HalFile& source, HalFile& html, HalFile& toc, std::string_view title) {
    serialization::BufferedFileWriter content(html, 2048), navigation(toc, 2048);
    // Parser is ~4 KB and lasts only for conversion, never for page turns.
    auto parser = makeUniqueNoThrow<Parser>(writeText, &content, writeHeading, &navigation);
    if (!parser) {
      LOG_ERR("MD", "OOM: Markdown parser");
      return false;
    }
    content.write(XHTML_START, sizeof(XHTML_START) - 1);
    Parser::escape(writeText, &content, title);
    content.write(XHTML_BODY, sizeof(XHTML_BODY) - 1);
    navigation.write(NCX_START, sizeof(NCX_START) - 1);
    Parser::escape(writeText, &navigation, title);
    constexpr char tocRoot[] = "</text></docTitle><navMap><navPoint id=\"document\" playOrder=\"1\"><navLabel><text>";
    navigation.write(tocRoot, sizeof(tocRoot) - 1);
    Parser::escape(writeText, &navigation, title);
    constexpr char tocLink[] = "</text></navLabel><content src=\"content.xhtml\"/></navPoint>\n";
    navigation.write(tocLink, sizeof(tocLink) - 1);
    size_t remaining = source.size();
    while (remaining) {
      const size_t count = std::min(remaining, sizeof(buffer));
      if (source.read(buffer, count) != static_cast<int>(count) ||
          !parser->feed(reinterpret_cast<char*>(buffer), count))
        return false;
      remaining -= count;
      vTaskDelay(1);
    }
    if (!parser->finish()) return false;
    constexpr char htmlEnd[] = "</body></html>", tocEnd[] = "</navMap></ncx>";
    content.write(htmlEnd, sizeof(htmlEnd) - 1);
    navigation.write(tocEnd, sizeof(tocEnd) - 1);
    return content.flush() && navigation.flush();
  }
  bool addFile(StoredZipWriter& zip, const char* name, const std::string& path) {
    HalFile input;
    if (!Storage.openFileForRead("MD", path, input) || !zip.begin(name)) return false;
    size_t remaining = input.size();
    while (remaining) {
      const size_t count = std::min(remaining, sizeof(buffer));
      if (input.read(buffer, count) != static_cast<int>(count) || !zip.append(buffer, count)) return false;
      remaining -= count;
      vTaskDelay(1);
    }
    return zip.end();
  }
  bool package(HalFile& out, const std::string& base, std::string_view title) {
    // Fixed ZIP directory state exceeds the small-stack local budget.
    auto zip = makeUniqueNoThrow<StoredZipWriter>(zipWrite, zipSeek, &out);
    if (!zip) {
      LOG_ERR("MD", "OOM: EPUB writer");
      return false;
    }
    constexpr char mime[] = "application/epub+zip";
    return zip->begin("mimetype") && zip->append(mime, sizeof(mime) - 1) && zip->end() &&
           zip->begin("META-INF/container.xml") && zip->append(CONTAINER, sizeof(CONTAINER) - 1) && zip->end() &&
           zip->begin("content.opf") && zip->append(OPF_START, sizeof(OPF_START) - 1) &&
           Parser::escape(zipText, zip.get(), title) && zip->append(OPF_END, sizeof(OPF_END) - 1) && zip->end() &&
           addFile(*zip, "toc.ncx", base + ".ncx.tmp") && addFile(*zip, "content.xhtml", base + ".html.tmp") &&
           zip->finish();
  }
};

bool build(Builder& builder, HalFile& source, const std::string& base, const std::string& title) {
  {
    HalFile html, toc;
    if (!Storage.openFileForWrite("MD", base + ".html.tmp", html) ||
        !Storage.openFileForWrite("MD", base + ".ncx.tmp", toc) || !builder.convert(source, html, toc, title))
      return false;
    // These files must be synchronized and closed before packaging reads them.
    if (!html.close() || !toc.close()) return false;
  }
  HalFile output;
  if (!Storage.openFileForWrite("MD", base + ".epub.tmp", output) || !builder.package(output, base, title))
    return false;
  return output.close();  // Sync before rename publishes the completed archive.
}
}  // namespace

bool prepareDocument(const std::string& sourcePath, std::string& archivePath) {
  // Never buffer the complete document: both passes use this reusable 2 KB workspace.
  auto builder = makeUniqueNoThrow<Builder>();
  if (!builder) {
    LOG_ERR("MD", "OOM: conversion buffer");
    return false;
  }
  HalFile source;
  if (!Storage.openFileForRead("MD", sourcePath, source)) return false;
  Fingerprint fingerprint;
  if (!builder->fingerprint(source, fingerprint)) return false;
  HalFile math;
  uint64_t mathHash = 0;
  const bool hasMath = builder->mathCopy(sourcePath, fingerprint, math, mathHash);
  if (hasMath) {
    fingerprint.hash ^= mathHash;
    fingerprint.version |= 0x80000000u;
  }
  const auto key = std::to_string(std::hash<std::string>{}(sourcePath));
  const std::string directory = "/.crosspoint/md_" + key;
  const std::string base = directory + "/document";
  archivePath = base + ".epub";
  {
    HalFile info;
    Fingerprint cached;
    if (Storage.exists((base + ".meta").c_str()) && Storage.openFileForRead("MD", base + ".meta", info) &&
        info.size() == sizeof(cached) && info.read(&cached, sizeof(cached)) == sizeof(cached) &&
        cached.version == fingerprint.version && cached.size == fingerprint.size && cached.hash == fingerprint.hash &&
        Storage.exists(archivePath.c_str()))
      return true;
  }
  if (!Storage.ensureDirectoryExists("/.crosspoint") || !Storage.ensureDirectoryExists(directory.c_str())) return false;
  const size_t slash = sourcePath.find_last_of('/');
  std::string title = sourcePath.substr(slash == std::string::npos ? 0 : slash + 1);
  const size_t dot = title.find_last_of('.');
  if (dot != std::string::npos) title.resize(dot);
  const bool built = hasMath ? builder->copyMath(math, base) : build(*builder, source, base, title);
  Storage.remove((base + ".html.tmp").c_str());
  Storage.remove((base + ".ncx.tmp").c_str());
  if (!built) {
    Storage.remove((base + ".epub.tmp").c_str());
    LOG_ERR("MD", "Markdown conversion failed");
    return false;
  }
  // Remove the marker first: interrupted publication is rebuilt on the next open.
  if (Storage.exists((base + ".meta").c_str()) && !Storage.remove((base + ".meta").c_str())) return false;
  if (Storage.exists(archivePath.c_str()) && !Storage.remove(archivePath.c_str())) return false;
  if (!Storage.rename((base + ".epub.tmp").c_str(), archivePath.c_str())) return false;
  const std::string layoutCache = "/.crosspoint/epub_" + key;
  if (Storage.exists(layoutCache.c_str()) && !Storage.removeDir(layoutCache.c_str())) return false;
  HalFile info;
  if (!Storage.openFileForWrite("MD", base + ".meta", info) ||
      info.write(&fingerprint, sizeof(fingerprint)) != sizeof(fingerprint))
    return false;
  return info.close();
}
}  // namespace markdown
