#pragma once

#include <string>

namespace markdown {
// Creates a cached EPUB from the Markdown source, retaining the source's reader identity.
bool prepareDocument(const std::string& sourcePath, std::string& archivePath);
}  // namespace markdown
