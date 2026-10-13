#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A highlighted run of text: [start, end) in visible-codepoint offsets within
// one spine item, the same basis as Page::visibleTextOffset, so it survives
// font, margin and orientation changes.
struct HighlightEntry {
  static constexpr std::size_t MAX_TEXT_LENGTH = 256;

  uint16_t spineIndex = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  std::string text;  // The highlighted words, truncated to MAX_TEXT_LENGTH bytes

  bool contains(const uint16_t spine, const uint32_t offset) const {
    return spine == spineIndex && offset >= start && offset < end;
  }
};

// Per-book highlight persistence in /.crosspoint/highlights/, named like the
// book's bookmark file.
namespace HighlightFile {

// The vector is cleared first; a missing file yields an empty list and returns false.
bool load(const std::string& bookPath, std::vector<HighlightEntry>& highlights);

bool save(const std::string& bookPath, const std::vector<HighlightEntry>& highlights);

}  // namespace HighlightFile
