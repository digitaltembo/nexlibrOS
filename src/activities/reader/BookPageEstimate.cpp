#include "BookPageEstimate.h"

#include <Epub.h>

#include <algorithm>
#include <cmath>

namespace {
// Chapters below this size (cover, image-only and title pages) say nothing
// about text density, so they stay out of the ratio.
constexpr size_t kMinRatioChapterBytes = 1024;
// Text that must be laid out before the rest of the book is scaled from it.
constexpr size_t kMinSampleBytes = 4096;

size_t spineBytes(const Epub& epub, const int spine) {
  const size_t end = epub.getCumulativeSpineItemSize(spine);
  const size_t start = spine > 0 ? epub.getCumulativeSpineItemSize(spine - 1) : 0;
  return end > start ? end - start : 0;
}
}  // namespace

void BookPageEstimate::record(const Epub& epub, const int spineIndex, const int pageCount) {
  const int spineCount = epub.getSpineItemsCount();
  if (spineIndex < 0 || spineIndex >= spineCount || pageCount <= 0) return;
  if (static_cast<int>(pages_.size()) != spineCount) pages_.assign(spineCount, 0);
  const auto count = static_cast<uint16_t>(std::min(pageCount, 0xFFFF));
  if (pages_[spineIndex] == count) return;
  if (pages_[spineIndex] != 0) std::fill(pages_.begin(), pages_.end(), 0);
  pages_[spineIndex] = count;
}

float BookPageEstimate::pagesPerByte(const Epub& epub, const int spineIndex, const int chapterPages) const {
  const int spineCount = epub.getSpineItemsCount();
  size_t sampleBytes = 0;
  int samplePages = 0;
  for (int i = 0; i < spineCount; ++i) {
    const int pages = i == spineIndex ? chapterPages : (i < static_cast<int>(pages_.size()) ? pages_[i] : 0);
    const size_t bytes = spineBytes(epub, i);
    if (pages <= 0 || bytes < kMinRatioChapterBytes) continue;
    sampleBytes += bytes;
    samplePages += pages;
  }
  if (sampleBytes < kMinSampleBytes) return 0.0f;
  return static_cast<float>(samplePages) / static_cast<float>(sampleBytes);
}

int BookPageEstimate::chapterPageCount(const Epub& epub, const int spine, const float ratio, const int spineIndex,
                                       const int chapterPages) const {
  if (spine == spineIndex && chapterPages > 0) return chapterPages;
  if (spine < static_cast<int>(pages_.size()) && pages_[spine] > 0) return pages_[spine];
  return std::max(1, static_cast<int>(std::lround(static_cast<float>(spineBytes(epub, spine)) * ratio)));
}

bool BookPageEstimate::position(const Epub& epub, const int spineIndex, const int pageIndex, const int chapterPages,
                                int& page, int& total) const {
  const int spineCount = epub.getSpineItemsCount();
  if (spineIndex < 0 || spineIndex >= spineCount) return false;
  const float ratio = pagesPerByte(epub, spineIndex, chapterPages);
  if (ratio <= 0.0f) return false;
  int before = 0;
  total = 0;
  for (int i = 0; i < spineCount; ++i) {
    if (i == spineIndex) before = total;
    total += chapterPageCount(epub, i, ratio, spineIndex, chapterPages);
  }
  page = std::clamp(before + pageIndex + 1, 1, std::max(1, total));
  return total > 0;
}

int BookPageEstimate::pageAt(const Epub& epub, const int spine, const float chapterFraction, const int spineIndex,
                             const int chapterPages) const {
  const int spineCount = epub.getSpineItemsCount();
  if (spine < 0 || spine >= spineCount) return 0;
  const float ratio = pagesPerByte(epub, spineIndex, chapterPages);
  if (ratio <= 0.0f) return 0;
  int before = 0;
  for (int i = 0; i < spine; ++i) before += chapterPageCount(epub, i, ratio, spineIndex, chapterPages);
  const int pages = chapterPageCount(epub, spine, ratio, spineIndex, chapterPages);
  const int index = std::clamp(static_cast<int>(chapterFraction * static_cast<float>(pages)), 0, pages - 1);
  return before + index + 1;
}

bool BookPageEstimate::locate(const Epub& epub, const int spineIndex, const int chapterPages, const int page,
                              int& targetSpine, float& chapterFraction) const {
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0) return false;
  const float ratio = pagesPerByte(epub, spineIndex, chapterPages);
  if (ratio <= 0.0f) return false;
  int before = 0;
  for (int i = 0; i < spineCount; ++i) {
    const int pages = chapterPageCount(epub, i, ratio, spineIndex, chapterPages);
    if (page <= before + pages || i == spineCount - 1) {
      targetSpine = i;
      const int index = std::clamp(page - before - 1, 0, pages - 1);
      chapterFraction = (static_cast<float>(index) + 0.5f) / static_cast<float>(pages);
      return true;
    }
    before += pages;
  }
  return false;
}
