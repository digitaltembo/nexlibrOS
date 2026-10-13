#include "HighlightMarks.h"

#include <GfxRenderer.h>

#include <algorithm>

void HighlightMarks::drawBackgrounds(const GfxRenderer& renderer, const Page& page, const int fontId,
                                     const int marginLeft, const int marginTop,
                                     const std::vector<HighlightEntry>& highlights, const uint16_t spineIndex) {
  if (std::none_of(highlights.begin(), highlights.end(),
                   [&](const HighlightEntry& h) { return h.spineIndex == spineIndex; })) {
    return;
  }

  const int ascender = renderer.getFontAscenderSize(fontId);
  const int lineHeight = renderer.getLineHeight(fontId);
  for (const auto& element : page.elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;

    const int top = line->yPos + marginTop + block->getRubyShift(ascender);
    int runStart = -1;
    int runEnd = -1;
    const auto flush = [&]() {
      if (runStart >= 0 && runEnd > runStart) {
        renderer.fillRectDither(runStart, top, runEnd - runStart, lineHeight, Color::LightGray);
      }
      runStart = -1;
      runEnd = -1;
    };

    // Words are stored in visual order, so a highlighted run is contiguous in index order.
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const uint32_t offset = block->wordVisibleOffset(i);
      const bool marked = std::any_of(highlights.begin(), highlights.end(),
                                      [&](const HighlightEntry& h) { return h.contains(spineIndex, offset); });
      if (!marked) {
        flush();
        continue;
      }
      const int x = line->xPos + block->wordXpos(i) + marginLeft;
      const int width = renderer.getTextAdvanceX(fontId, block->wordText(i), block->wordStyle(i),
                                                 block->getBlockStyle().characterSpacing);
      if (runStart < 0) {
        runStart = x;
        runEnd = x + width;
      } else {
        runStart = std::min(runStart, x);
        runEnd = std::max(runEnd, x + width);
      }
    }
    flush();
  }
}
