#include "LuaImage.h"

#include <Bitmap.h>
#include <Epub/converters/DirectPixelWriter.h>
#include <Epub/converters/ImageDecoderFactory.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <string>
#include <string_view>

namespace {

enum class Format : uint8_t { Unknown, Pxc, Bmp, Decoded };

Format formatOf(const char* path) {
  const std::string_view name(path);
  if (FsHelpers::checkFileExtension(name, ".pxc")) return Format::Pxc;
  if (FsHelpers::hasBmpExtension(name)) return Format::Bmp;
  if (FsHelpers::hasJpgExtension(name) || FsHelpers::hasPngExtension(name)) return Format::Decoded;
  return Format::Unknown;
}

constexpr size_t PXC_HEADER_BYTES = 4;
// Rows are read in batches of about this many bytes.
constexpr size_t PXC_READ_BYTES = 4096;

// Reads and checks a .pxc header: the file must hold every row it declares.
const char* readPxcHeader(HalFile& file, int& width, int& height) {
  uint16_t w = 0;
  uint16_t h = 0;
  if (file.read(&w, 2) != 2 || file.read(&h, 2) != 2 || w == 0 || h == 0) return "corrupt image cache";
  const size_t bytesPerRow = (w + 3) / 4;
  if (file.size() < PXC_HEADER_BYTES + bytesPerRow * h) return "corrupt image cache";
  width = w;
  height = h;
  return nullptr;
}

bool onScreen(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  return x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= renderer.getScreenWidth() &&
         y + height <= renderer.getScreenHeight();
}

const char* drawPxc(GfxRenderer& renderer, const char* path, const int x, const int y) {
  HalFile file;
  if (!Storage.openFileForRead("LUA", path, file)) return "cannot open";
  int width = 0;
  int height = 0;
  if (const char* err = readPxcHeader(file, width, height)) return err;
  if (!onScreen(renderer, x, y, width, height)) return "off screen";

  const size_t bytesPerRow = (width + 3) / 4;
  int rowsPerRead = static_cast<int>(PXC_READ_BYTES / bytesPerRow);
  if (rowsPerRead < 1) rowsPerRead = 1;
  if (rowsPerRead > height) rowsPerRead = height;
  auto rows = makeUniqueNoThrow<uint8_t[]>(rowsPerRead * bytesPerRow);
  if (!rows) {
    LOG_ERR("LUA", "OOM: pxc rows (%u bytes)", static_cast<unsigned>(rowsPerRead * bytesPerRow));
    return "not enough memory";
  }

  DirectPixelWriter pw;
  pw.init(renderer);
  for (int row = 0; row < height; row += rowsPerRead) {
    const int count = height - row < rowsPerRead ? height - row : rowsPerRead;
    const size_t bytes = count * bytesPerRow;
    if (file.read(rows.get(), bytes) != static_cast<int>(bytes)) return "read failed";
    for (int i = 0; i < count; i++) {
      const uint8_t* rowBytes = rows.get() + i * bytesPerRow;
      pw.beginRow(y + row + i);
      int colStart = 0;
      int colEnd = 0;
      pw.bandColRange(x, width, colStart, colEnd);
      for (int col = colStart; col < colEnd; col++) {
        pw.writePixel(x + col, (rowBytes[col >> 2] >> (6 - (col & 3) * 2)) & 0x03);
      }
    }
  }
  renderer.preserveImagePolarity(x, y, width, height);
  return nullptr;
}

const char* drawBmp(GfxRenderer& renderer, const char* path, const int x, const int y, const int width,
                    const int height) {
  HalFile file;
  if (!Storage.openFileForRead("LUA", path, file)) return "cannot open";
  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return "unsupported bmp";
  if (!renderer.drawBitmap(bitmap, x, y, width, height)) return "draw failed";
  return nullptr;
}

RenderConfig exactConfig(const int x, const int y, const int width, const int height) {
  RenderConfig config;
  config.x = x;
  config.y = y;
  config.maxWidth = width;
  config.maxHeight = height;
  config.useExactDimensions = true;
  return config;
}

}  // namespace

namespace luaimage {

const char* size(const char* path, int& width, int& height) {
  switch (formatOf(path)) {
    case Format::Pxc: {
      HalFile file;
      if (!Storage.openFileForRead("LUA", path, file)) return "cannot open";
      return readPxcHeader(file, width, height);
    }
    case Format::Bmp: {
      HalFile file;
      if (!Storage.openFileForRead("LUA", path, file)) return "cannot open";
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) return "unsupported bmp";
      width = bitmap.getWidth();
      height = bitmap.getHeight();
      return nullptr;
    }
    case Format::Decoded: {
      const std::string source(path);
      ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(source);
      ImageDimensions dims{};
      if (!decoder || !decoder->getDimensions(source, dims) || dims.width <= 0 || dims.height <= 0) {
        return "unreadable image";
      }
      width = dims.width;
      height = dims.height;
      return nullptr;
    }
    case Format::Unknown:
      break;
  }
  return "unsupported format";
}

const char* draw(GfxRenderer& renderer, const char* path, const int x, const int y, const int width, const int height) {
  const Format format = formatOf(path);
  if (format == Format::Pxc) return drawPxc(renderer, path, x, y);
  if (!onScreen(renderer, x, y, width, height)) return "off screen";
  if (format == Format::Bmp) return drawBmp(renderer, path, x, y, width, height);
  if (format != Format::Decoded) return "unsupported format";

  const std::string source(path);
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(source);
  if (!decoder || !decoder->decodeToFramebuffer(source, renderer, exactConfig(x, y, width, height))) {
    return "decode failed";
  }
  renderer.preserveImagePolarity(x, y, width, height);
  return nullptr;
}

const char* writeCache(GfxRenderer& renderer, const char* src, const char* cachePath, const int width,
                       const int height) {
  if (formatOf(src) != Format::Decoded) return "unsupported format";
  if (formatOf(cachePath) != Format::Pxc) return "cache path must end in .pxc";
  if (!onScreen(renderer, 0, 0, width, height)) return "larger than the screen";

  const std::string source(src);
  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(source);
  if (!decoder) return "unsupported format";
  RenderConfig config = exactConfig(0, 0, width, height);
  config.cachePath = cachePath;
  const bool decoded = decoder->decodeToFramebuffer(source, renderer, config);

  // The decoder still paints when it cannot write the cache, so check the file.
  int cachedWidth = 0;
  int cachedHeight = 0;
  if (decoded && size(cachePath, cachedWidth, cachedHeight) == nullptr && abs(cachedWidth - width) <= 1 &&
      abs(cachedHeight - height) <= 1) {
    return nullptr;
  }
  Storage.remove(cachePath);
  return decoded ? "cache write failed" : "decode failed";
}

}  // namespace luaimage
