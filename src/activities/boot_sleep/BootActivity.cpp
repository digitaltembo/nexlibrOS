#include "BootActivity.h"

#include <GfxRenderer.h>

#include "images/BootImage.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int originX = (pageWidth - BOOT_IMAGE_WIDTH) / 2;
  const int originY = (pageHeight - BOOT_IMAGE_HEIGHT) / 2;
  constexpr int rowBytes = (BOOT_IMAGE_WIDTH + 7) / 8;

  renderer.clearScreen();
  // Plot per pixel so orientation and panel size are handled by drawPixel;
  // drawImage blits unrotated panel-space bytes.
  for (int row = 0; row < BOOT_IMAGE_HEIGHT; row++) {
    const int y = originY + row;
    if (y < 0 || y >= pageHeight) continue;
    for (int col = 0; col < BOOT_IMAGE_WIDTH; col++) {
      const int x = originX + col;
      if (x < 0 || x >= pageWidth) continue;
      if (!(BootImage[row * rowBytes + col / 8] & (0x80 >> (col % 8)))) {
        renderer.drawPixel(x, y, true);
      }
    }
  }
  renderer.displayBuffer();
}
