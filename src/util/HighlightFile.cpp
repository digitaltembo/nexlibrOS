#include "HighlightFile.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include "BookmarkUtil.h"

namespace {

constexpr char HIGHLIGHTS_DIR[] = "/.crosspoint/highlights/";

std::string highlightPath(const std::string& bookPath) {
  // Same flattened filename as the bookmark file, in a sibling directory.
  const std::string bookmarkPath = BookmarkUtil::getBookmarkPath(bookPath);
  return std::string(HIGHLIGHTS_DIR) + bookmarkPath.substr(BookmarkUtil::getBookmarksDir().size());
}

}  // namespace

bool HighlightFile::load(const std::string& bookPath, std::vector<HighlightEntry>& highlights) {
  highlights.clear();

  const std::string path = highlightPath(bookPath);
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(path.c_str(), doc)) {
    return false;
  }

  JsonArray arr = doc["highlights"].as<JsonArray>();
  highlights.reserve(arr.size());
  for (JsonObject obj : arr) {
    HighlightEntry entry;
    entry.spineIndex = obj["si"] | static_cast<uint16_t>(0);
    entry.start = obj["s"] | static_cast<uint32_t>(0);
    entry.end = obj["e"] | static_cast<uint32_t>(0);
    entry.text = obj["text"] | "";
    if (entry.text.size() > HighlightEntry::MAX_TEXT_LENGTH) entry.text.resize(HighlightEntry::MAX_TEXT_LENGTH);
    if (entry.end <= entry.start) continue;
    highlights.push_back(std::move(entry));
  }

  LOG_DBG("HLT", "Loaded %zu highlights", highlights.size());
  return true;
}

void HighlightFile::retire(const std::string& bookPath) {
  const std::string path = highlightPath(bookPath);
  // Never overwrites an earlier set-aside file: it may hold entries that did not fit.
  std::string target = path + ".migrated";
  for (int n = 1; Storage.exists(target.c_str()) && n < 100; n++) {
    target = path + ".migrated." + std::to_string(n);
  }
  if (!Storage.rename(path.c_str(), target.c_str())) {
    LOG_ERR("HLT", "Failed to set aside %s", path.c_str());
  }
}
