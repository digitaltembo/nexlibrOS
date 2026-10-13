#include "BookmarkMarker.h"

#include <GfxRenderer.h>

namespace {

constexpr int CORNER_SIZE = 44;  // length of each leg, in logical pixels
constexpr int TAP_SIZE = 80;     // side of the square tap target in the corner

}  // namespace

void BookmarkMarker::draw(const GfxRenderer& renderer) {
  // Right triangle whose legs run along the top and right screen edges,
  // ignoring the viewable-area margins so it sits flush in the corner.
  const int edge = renderer.getScreenWidth();
  for (int row = 0; row < CORNER_SIZE; row++) {
    const int width = CORNER_SIZE - row;
    renderer.fillRectDither(edge - width, row, width, 1, Color::LightGray);
  }
}

bool BookmarkMarker::hitTest(const GfxRenderer& renderer, const int x, const int y) {
  return x >= renderer.getScreenWidth() - TAP_SIZE && y < TAP_SIZE;
}
