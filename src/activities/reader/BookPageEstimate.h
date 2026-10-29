#pragma once

#include <cstdint>
#include <vector>

class Epub;

// Whole-book page numbers for an EPUB, which is only ever paginated one
// chapter at a time. Chapters laid out this session contribute their exact
// page counts; the rest are scaled from their byte size by the pages-per-byte
// of the laid-out chapters. The numbers firm up as more chapters are seen.
class BookPageEstimate {
 public:
  // Exact page count of a fully laid-out chapter. A count that differs from
  // the one already recorded means the layout changed (font, margins,
  // orientation), so every other chapter's count is dropped as stale.
  void record(const Epub& epub, int spineIndex, int pageCount);

  // 1-based book page and total for page `pageIndex` of `spineIndex`, whose
  // chapter currently has `chapterPages` (exact or estimated). False until
  // enough text has been laid out to scale the rest of the book from.
  bool position(const Epub& epub, int spineIndex, int pageIndex, int chapterPages, int& page, int& total) const;

  // 1-based book page at `chapterFraction` through `spine`, with the current
  // chapter (`spineIndex`, `chapterPages`) counted as in position(); 0 when
  // there is not enough laid out to estimate from.
  int pageAt(const Epub& epub, int spine, float chapterFraction, int spineIndex, int chapterPages) const;

  // Inverse of position(): the chapter holding 1-based book `page` and the
  // fraction through that chapter where it starts (centred on the page, so a
  // re-paginated chapter still lands on it).
  bool locate(const Epub& epub, int spineIndex, int chapterPages, int page, int& targetSpine,
              float& chapterFraction) const;

 private:
  // Pages per byte from the recorded chapters plus the current one; 0 when the
  // sample is too small to trust.
  float pagesPerByte(const Epub& epub, int spineIndex, int chapterPages) const;
  // Exact or scaled page count of one chapter.
  int chapterPageCount(const Epub& epub, int spine, float ratio, int spineIndex, int chapterPages) const;

  std::vector<uint16_t> pages_;  // per spine item; 0 = not laid out this session
};
