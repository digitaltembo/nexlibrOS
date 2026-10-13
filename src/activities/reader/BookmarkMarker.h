#pragma once

class GfxRenderer;

namespace BookmarkMarker {

// Marks a bookmarked page with a light gray triangle filling the top-right
// corner of the screen. The dither writes white as well as black, so
// draw before the text.
void draw(const GfxRenderer& renderer);

// True when a tap at (x, y) lands on the corner's tap target, which is a
// finger-sized square that is larger than the drawn triangle.
bool hitTest(const GfxRenderer& renderer, int x, int y);

}  // namespace BookmarkMarker
