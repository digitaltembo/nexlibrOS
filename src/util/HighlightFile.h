#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// A highlight saved before highlights moved to ClippingStore: [start, end) in
// visible-codepoint offsets within one spine item.
struct HighlightEntry {
  static constexpr std::size_t MAX_TEXT_LENGTH = 256;

  uint16_t spineIndex = 0;
  uint32_t start = 0;
  uint32_t end = 0;
  std::string text;
};

// Reads the old per-book files in /.crosspoint/highlights/ so they can be migrated.
namespace HighlightFile {

// The vector is cleared first; a missing file yields an empty list and returns false.
bool load(const std::string& bookPath, std::vector<HighlightEntry>& highlights);

// Renames the book's file so it is not migrated again.
void retire(const std::string& bookPath);

}  // namespace HighlightFile
