#pragma once

class GfxRenderer;

// Image files for Lua apps: JPEG and PNG (decoded and dithered on every
// draw, or once into a .pxc cache), 2/24-bit BMP, and .pxc pixel caches (see
// docs/files/pxc.ksy). Draws honor the renderer's render mode, so the same
// call fills the BW frame and each grayscale plane. Functions return nullptr
// on success or a short error for the app.
namespace luaimage {

// Pixel size of the image at path.
const char* size(const char* path, int& width, int& height);

// Draws the image with its top-left corner at (x, y): JPEG and PNG scaled to
// exactly width x height, BMP scaled down to fit within width x height, .pxc at
// its own size. The drawn area must lie on screen.
const char* draw(GfxRenderer& renderer, const char* path, int x, int y, int width, int height);

// Decodes the JPEG or PNG at src, scaled to width x height (at most the screen
// size), into a .pxc at cachePath. The decoder also paints the framebuffer, so
// call with RenderLock held and redraw before the next refresh.
const char* writeCache(GfxRenderer& renderer, const char* src, const char* cachePath, int width, int height);

}  // namespace luaimage
