#pragma once

#include <Epub/Page.h>

#include <cstdint>
#include <vector>

#include "util/HighlightFile.h"

class GfxRenderer;

namespace HighlightMarks {

// Shades the highlighted words of `page` (spine item `spineIndex`) with a
// light gray band per run, continuous across the gaps between words. The
// dither writes white as well as black, so call it before the page's text.
void drawBackgrounds(const GfxRenderer& renderer, const Page& page, int fontId, int marginLeft, int marginTop,
                     const std::vector<HighlightEntry>& highlights, uint16_t spineIndex);

}  // namespace HighlightMarks
