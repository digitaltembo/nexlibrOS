#include "BookTextIndex.h"

#include <Epub.h>
#include <Epub/VisibleTextUtils.h>
#include <Epub/htmlEntities.h>
#include <Logging.h>
#include <Print.h>

#include <algorithm>
#include <cstring>

namespace BookTextIndex {

namespace {
constexpr char MAGIC[4] = {'C', 'P', 'T', 'X'};
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = 8;  // magic, version, reserved, u16 spine count
constexpr size_t STREAM_CHUNK = 1024;

void XMLCALL startElement(void* user, const XML_Char* name, const XML_Char**) {
  static_cast<Builder*>(user)->onStart(name);
}
void XMLCALL endElement(void* user, const XML_Char* name) { static_cast<Builder*>(user)->onEnd(name); }
void XMLCALL characterData(void* user, const XML_Char* s, const int len) {
  static_cast<Builder*>(user)->onText(s, len);
}
// Same entity handling as ChapterHtmlSlimParser::defaultHandlerExpand, so the
// counted text matches the layout's: known HTML entities expand, unknown ones
// stay literal, everything else the default handler sees is dropped.
void XMLCALL defaultHandlerExpand(void* user, const XML_Char* s, const int len) {
  if (len < 3 || s[0] != '&' || s[len - 1] != ';') return;
  const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
  if (utf8Value) {
    characterData(user, utf8Value, static_cast<int>(strlen(utf8Value)));
  } else {
    characterData(user, s, len);
  }
}

// Feeds the inflated item straight into expat. A parse error (commonly
// trailing junk after </html>) keeps the text extracted so far.
class ExpatFeed final : public Print {
 public:
  explicit ExpatFeed(XML_Parser parser) : parser(parser) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t len) override {
    if (!failed &&
        XML_Parse(parser, reinterpret_cast<const char*>(buf), static_cast<int>(len), XML_FALSE) == XML_STATUS_ERROR) {
      failed = true;
    }
    return len;
  }
  void finish() {
    if (!failed) XML_Parse(parser, nullptr, 0, XML_TRUE);
  }

 private:
  XML_Parser parser;
  bool failed = false;
};

void writeU32(uint8_t* out, const uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

uint32_t readU32(const uint8_t* in) {
  return static_cast<uint32_t>(in[0]) | (static_cast<uint32_t>(in[1]) << 8) | (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}
}  // namespace

std::string path(const Epub& epub) { return epub.getCachePath() + "/text.bin"; }

Builder::~Builder() { abandon(); }

void Builder::abandon() {
  if (!file.isOpen()) return;
  file.close();
  Storage.remove(tmpPath.c_str());
}

bool Builder::begin(const std::shared_ptr<Epub>& source) {
  epub = source;
  const int spines = epub->getSpineItemsCount();
  if (spines <= 0 || spines > 0xFFFF) return false;
  tmpPath = path(*epub) + ".tmp";
  if (!Storage.openFileForWrite("BTI", tmpPath, file)) return false;
  offsets.assign(spines + 1, 0);
  // Header and a zeroed offset table; finish() fills the table in.
  uint8_t header[HEADER_SIZE] = {MAGIC[0],
                                 MAGIC[1],
                                 MAGIC[2],
                                 MAGIC[3],
                                 VERSION,
                                 0,
                                 static_cast<uint8_t>(spines),
                                 static_cast<uint8_t>(spines >> 8)};
  const uint8_t zero[4] = {};
  bool ok = file.write(header, sizeof(header)) == sizeof(header);
  for (int i = 0; ok && i <= spines; ++i) ok = file.write(zero, sizeof(zero)) == sizeof(zero);
  if (!ok) {
    LOG_ERR("BTI", "Failed to write text index header");
    abandon();
    return false;
  }
  position = static_cast<uint32_t>(HEADER_SIZE + 4 * (spines + 1));
  offsets[0] = position;
  return true;
}

bool Builder::addSpine(const int spineIndex) {
  if (!file.isOpen() || spineIndex < 0 || spineIndex >= spineCount()) return false;
  insideBody = false;
  nonVisibleDepth = 0;
  textBuffered = 0;
  const auto href = epub->getSpineItem(spineIndex).href;
  if (!href.empty()) {
    XML_Parser parser = XML_ParserCreate(nullptr);
    if (!parser) {
      LOG_ERR("BTI", "OOM: expat parser");
      abandon();
      return false;
    }
    XML_SetUserData(parser, this);
    XML_SetDefaultHandlerExpand(parser, defaultHandlerExpand);
    XML_SetElementHandler(parser, startElement, endElement);
    XML_SetCharacterDataHandler(parser, characterData);
    ExpatFeed feed(parser);
    // An unreadable item is indexed as empty rather than failing the book.
    if (!epub->readItemContentsToStream(href, feed, STREAM_CHUNK)) {
      LOG_ERR("BTI", "Failed to read spine %d for the text index", spineIndex);
    }
    feed.finish();
    XML_ParserFree(parser);
    flushText();
  }
  if (!file.isOpen()) return false;  // a write failed
  offsets[spineIndex + 1] = position;
  return true;
}

bool Builder::finish() {
  if (!file.isOpen()) return false;
  bool ok = file.seek(HEADER_SIZE);
  for (size_t i = 0; ok && i < offsets.size(); ++i) {
    uint8_t value[4];
    writeU32(value, offsets[i]);
    ok = file.write(value, sizeof(value)) == sizeof(value);
  }
  if (!ok) {
    LOG_ERR("BTI", "Failed to write text index table");
    abandon();
    return false;
  }
  file.close();
  const std::string finalPath = path(*epub);
  if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("BTI", "Failed to move text index into place");
    Storage.remove(tmpPath.c_str());
    return false;
  }
  return true;
}

void Builder::onStart(const char* name) {
  if (strcasecmp(name, "body") == 0) insideBody = true;
  if (insideBody && (nonVisibleDepth > 0 || VisibleTextUtils::isNonVisibleElement(name))) nonVisibleDepth++;
}

void Builder::onEnd(const char* name) {
  if (nonVisibleDepth > 0) nonVisibleDepth--;
  if (strcmp(name, "body") == 0) insideBody = false;
}

void Builder::onText(const char* s, int len) {
  if (!insideBody || nonVisibleDepth > 0 || !file.isOpen()) return;
  while (len > 0) {
    const size_t take = std::min(static_cast<size_t>(len), TEXT_BUFFER_SIZE - textBuffered);
    memcpy(textBuffer + textBuffered, s, take);
    textBuffered += take;
    s += take;
    len -= static_cast<int>(take);
    if (textBuffered == TEXT_BUFFER_SIZE) flushText();
  }
}

void Builder::flushText() {
  if (textBuffered == 0 || !file.isOpen()) return;
  if (file.write(textBuffer, textBuffered) != textBuffered) {
    LOG_ERR("BTI", "Failed to write text index");
    abandon();
    return;
  }
  position += static_cast<uint32_t>(textBuffered);
  textBuffered = 0;
}

bool Reader::open(const Epub& epub) {
  offsets.clear();
  const std::string indexPath = path(epub);
  if (!Storage.exists(indexPath.c_str()) || !Storage.openFileForRead("BTI", indexPath, handle)) return false;
  uint8_t header[HEADER_SIZE];
  if (handle.read(header, sizeof(header)) != static_cast<int>(sizeof(header)) || memcmp(header, MAGIC, 4) != 0 ||
      header[4] != VERSION) {
    handle.close();
    return false;
  }
  const int spines = header[6] | (header[7] << 8);
  if (spines != epub.getSpineItemsCount()) {
    handle.close();
    return false;
  }
  offsets.reserve(spines + 1);
  for (int i = 0; i <= spines; ++i) {
    uint8_t value[4];
    if (handle.read(value, sizeof(value)) != static_cast<int>(sizeof(value))) {
      handle.close();
      offsets.clear();
      return false;
    }
    offsets.push_back(readU32(value));
  }
  // A torn write leaves the table zeroed or out of order.
  for (int i = 0; i < spines; ++i) {
    if (offsets[i] == 0 || offsets[i] > offsets[i + 1]) {
      handle.close();
      offsets.clear();
      return false;
    }
  }
  return true;
}

}  // namespace BookTextIndex
