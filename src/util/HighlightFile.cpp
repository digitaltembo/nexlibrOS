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

bool HighlightFile::save(const std::string& bookPath, const std::vector<HighlightEntry>& highlights) {
  JsonDocument doc;
  JsonArray arr = doc["highlights"].to<JsonArray>();
  for (const auto& entry : highlights) {
    JsonObject obj = arr.add<JsonObject>();
    obj["si"] = entry.spineIndex;
    obj["s"] = entry.start;
    obj["e"] = entry.end;
    obj["text"] = entry.text;
  }

  // writeDocToFile ensures /.crosspoint; the highlights subdirectory is ours.
  Storage.mkdir(HIGHLIGHTS_DIR);
  const std::string path = highlightPath(bookPath);
  if (!PersistableStoreBase::writeDocToFile(path.c_str(), doc)) {
    LOG_ERR("HLT", "Failed to save %zu highlights", highlights.size());
    return false;
  }
  return true;
}
