#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "BookTextIndex.h"

class Epub;

// Incremental full-text search over a BookTextIndex. Matching ignores ASCII and
// Latin-1 case, treats any run of whitespace as one space and curly quotes as
// straight ones; offsets are raw codepoint offsets into the spine item's
// visible text (the reader's visibleTextOffset).
class BookTextSearch {
 public:
  struct Match {
    uint16_t spine = 0;
    uint32_t offset = 0;           // visible-text codepoint offset within the spine item
    float chapterFraction = 0.0f;  // offset / the spine item's visible length
    std::string snippet;           // whitespace-collapsed context around the match
  };

  static constexpr size_t MAX_MATCHES = 100;
  static constexpr size_t MAX_TERM_CODEPOINTS = 48;

  // False when the index cannot be opened or the term has no searchable text.
  bool begin(const Epub& epub, const std::string& term);
  // Scans up to `budget` bytes of text; false once the search is finished.
  bool step(size_t budget);
  int progressPercent() const;
  // Hit MAX_MATCHES and stopped early.
  bool truncated() const { return capped; }
  std::vector<Match>& matches() { return results; }

 private:
  void feedCodepoint(uint32_t cp);
  void endSpine();
  void appendToPending(uint32_t displayCp);
  void finishSnippet(Match& match) const;

  BookTextIndex::Reader index;
  std::vector<Match> results;
  bool capped = false;

  // Folded search term.
  uint32_t needle[MAX_TERM_CODEPOINTS] = {};
  uint8_t needleLength = 0;

  // Scan position.
  int spine = 0;
  uint32_t filePos = 0;
  uint32_t spineCodepoints = 0;  // raw codepoints seen in this spine item
  size_t spineFirstMatch = 0;    // first result belonging to this spine item
  uint32_t utf8Codepoint = 0;    // partial UTF-8 sequence across reads
  uint8_t utf8Remaining = 0;
  uint32_t utf8Offset = 0;  // raw offset of the partial sequence's first byte

  // Last folded codepoints and their raw offsets, for matching.
  uint32_t recentFolded[MAX_TERM_CODEPOINTS] = {};
  uint32_t recentOffsets[MAX_TERM_CODEPOINTS] = {};
  uint8_t recentHead = 0;
  uint8_t recentCount = 0;
  bool lastWasSpace = true;

  // Last display codepoints (whitespace collapsed, case kept), for the text
  // before a match.
  static constexpr size_t CONTEXT_BEFORE = 40;
  static constexpr size_t CONTEXT_AFTER = 70;
  static constexpr size_t CONTEXT_RING = CONTEXT_BEFORE + MAX_TERM_CODEPOINTS;
  uint32_t context[CONTEXT_RING] = {};
  uint8_t contextHead = 0;
  uint8_t contextCount = 0;

  // Matches still collecting the text after them.
  struct Pending {
    size_t result;
    uint8_t remaining;
  };
  std::vector<Pending> pending;

  uint8_t buffer[512];
};
