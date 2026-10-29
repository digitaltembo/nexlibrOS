#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Epub;

// The visible text of every spine item, extracted once into
// <book cache>/text.bin for full-text search. Text is counted exactly like the
// layout engine counts Page::visibleTextOffset (codepoints of <body> character
// data outside non-visible elements, HTML entities expanded), so a codepoint
// offset into a spine item's text is a position the reader can jump to.
//
// File: "CPTX", u8 version, u8 reserved, u16 spineCount, then spineCount + 1
// u32 absolute file positions (spine i's text is [pos[i], pos[i + 1])), then
// the UTF-8 text.
namespace BookTextIndex {

std::string path(const Epub& epub);

// Incremental build, one spine item per addSpine() so the caller can show
// progress and stay responsive. Writes a temp file renamed into place by
// finish(); an abandoned build leaves no index behind.
class Builder {
 public:
  ~Builder();
  bool begin(const std::shared_ptr<Epub>& epub);
  bool addSpine(int spineIndex);
  bool finish();
  int spineCount() const { return static_cast<int>(offsets.size()) - 1; }

  // Expat callbacks (public for the C trampolines).
  void onStart(const char* name);
  void onEnd(const char* name);
  void onText(const char* s, int len);

 private:
  void flushText();
  void abandon();

  std::shared_ptr<Epub> epub;
  std::string tmpPath;
  HalFile file;
  std::vector<uint32_t> offsets;  // spineCount + 1 file positions
  uint32_t position = 0;
  // Parse state for the spine item being extracted.
  bool insideBody = false;
  uint16_t nonVisibleDepth = 0;
  // Text is written in chunks: per-callback writes would take the storage
  // mutex for every few bytes.
  static constexpr size_t TEXT_BUFFER_SIZE = 512;
  char textBuffer[TEXT_BUFFER_SIZE];
  size_t textBuffered = 0;
};

// Read access for search.
class Reader {
 public:
  bool open(const Epub& epub);
  int spineCount() const { return static_cast<int>(offsets.size()) - 1; }
  uint32_t spineStart(int spine) const { return offsets[spine]; }
  uint32_t spineEnd(int spine) const { return offsets[spine + 1]; }
  uint32_t textStart() const { return offsets.empty() ? 0 : offsets.front(); }
  uint32_t textEnd() const { return offsets.empty() ? 0 : offsets.back(); }
  HalFile& file() { return handle; }

 private:
  HalFile handle;
  std::vector<uint32_t> offsets;
};

}  // namespace BookTextIndex
