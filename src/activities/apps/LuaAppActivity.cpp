#include "LuaAppActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <cstring>
#include <lua.hpp>
#include <variant>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "lua/LuaImage.h"

namespace {
constexpr const char* BUTTON_NAMES[] = {"back", "confirm", "left", "right", "up", "down"};
constexpr MappedInputManager::Button BUTTONS[] = {MappedInputManager::Button::Back, MappedInputManager::Button::Confirm,
                                                  MappedInputManager::Button::Left, MappedInputManager::Button::Right,
                                                  MappedInputManager::Button::Up,   MappedInputManager::Button::Down};
static_assert(sizeof(BUTTON_NAMES) / sizeof(BUTTON_NAMES[0]) == sizeof(BUTTONS) / sizeof(BUTTONS[0]));

// Direction the finger moved, indexed by the detail of a Swipe event.
constexpr const char* SWIPE_NAMES[] = {"left", "right", "up", "down"};
constexpr MappedInputManager::SwipeDir SWIPES[] = {
    MappedInputManager::SwipeDir::Left, MappedInputManager::SwipeDir::Right, MappedInputManager::SwipeDir::Up,
    MappedInputManager::SwipeDir::Down};
static_assert(sizeof(SWIPE_NAMES) / sizeof(SWIPE_NAMES[0]) == sizeof(SWIPES) / sizeof(SWIPES[0]));

constexpr const char* FONT_NAMES[] = {"ui",      "ui_large", "small",   "sans12",  "sans14", "sans16", "sans18",
                                      "serif12", "serif14",  "serif16", "serif18", "reader", nullptr};
// "reader" (READER_FONT_ID) resolves to the reading font family and size from settings.
// Noto Serif is no longer built in, so the serif names draw with Noto Sans.
constexpr int READER_FONT_ID = 0;
constexpr int FONT_IDS[] = {UI_10_FONT_ID,       UI_12_FONT_ID,       SMALL_FONT_ID,       NOTOSANS_12_FONT_ID,
                            NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID, NOTOSANS_12_FONT_ID,
                            NOTOSANS_14_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_18_FONT_ID, READER_FONT_ID};
static_assert(sizeof(FONT_NAMES) / sizeof(FONT_NAMES[0]) == sizeof(FONT_IDS) / sizeof(FONT_IDS[0]) + 1);
constexpr const char* STYLE_NAMES[] = {"regular", "bold", "italic", "bold_italic", nullptr};
constexpr const char* REFRESH_NAMES[] = {"fast", "half", "full", nullptr};
constexpr HalDisplay::RefreshMode REFRESH_MODES[] = {HalDisplay::FAST_REFRESH, HalDisplay::HALF_REFRESH,
                                                     HalDisplay::FULL_REFRESH};

constexpr int MAX_POLYGON_POINTS = 256;
constexpr int MAX_WRAP_LINES = 256;
constexpr size_t MAX_KEYBOARD_TEXT = 4096;

struct EventCall {
  LuaAppActivity* self;
  const void* event;
};
}  // namespace

// Lua bindings for the `gfx` and `sys` modules. Each function recovers the
// activity through LuaRuntime::host(). Drawing functions are only valid inside
// the app's draw() callback, which runs with RenderLock held.
struct LuaAppBindings {
  static LuaAppActivity& self(lua_State* L) { return *static_cast<LuaAppActivity*>(LuaRuntime::from(L)->host()); }

  static LuaAppActivity& drawing(lua_State* L) {
    LuaAppActivity& app = self(L);
    if (!app.inDraw) luaL_error(L, "gfx drawing is only allowed inside draw()");
    return app;
  }

  static int checkInt(lua_State* L, const int idx) { return static_cast<int>(luaL_checknumber(L, idx)); }
  static bool optBlack(lua_State* L, const int idx) { return lua_isnoneornil(L, idx) ? true : lua_toboolean(L, idx); }

  // Fill colour: a boolean (true black, false white) or one of the names below;
  // the greys are dithered. Defaults to black.
  static Color optColor(lua_State* L, const int idx) {
    if (lua_type(L, idx) != LUA_TSTRING) return optBlack(L, idx) ? Color::Black : Color::White;
    static const char* const NAMES[] = {"black", "white", "light_gray", "dark_gray", nullptr};
    static constexpr Color COLORS[] = {Color::Black, Color::White, Color::LightGray, Color::DarkGray};
    return COLORS[luaL_checkoption(L, idx, nullptr, NAMES)];
  }

  // Reads {x1, y1, x2, y2, ...} at idx into userdata-backed arrays (GC-owned
  // scratch, so a later Lua error cannot leak them). Returns the point count.
  static int readPoints(lua_State* L, const int idx, int** xs, int** ys) {
    luaL_checktype(L, idx, LUA_TTABLE);
    const lua_Integer len = luaL_len(L, idx);
    if (len < 4 || len % 2 != 0) luaL_argerror(L, idx, "expected {x1, y1, x2, y2, ...}");
    const int count = static_cast<int>(len / 2);
    if (count > MAX_POLYGON_POINTS) luaL_argerror(L, idx, "too many points");
    auto* buf = static_cast<int*>(lua_newuserdatauv(L, sizeof(int) * 2 * count, 0));
    for (int i = 0; i < count; i++) {
      lua_rawgeti(L, idx, 2 * i + 1);
      lua_rawgeti(L, idx, 2 * i + 2);
      buf[i] = static_cast<int>(luaL_checknumber(L, -2));
      buf[count + i] = static_cast<int>(luaL_checknumber(L, -1));
      lua_pop(L, 2);
    }
    *xs = buf;
    *ys = buf + count;
    return count;
  }

  // --- gfx -------------------------------------------------------------------
  static int width(lua_State* L) {
    lua_pushinteger(L, self(L).renderer.getScreenWidth());
    return 1;
  }
  static int height(lua_State* L) {
    lua_pushinteger(L, self(L).renderer.getScreenHeight());
    return 1;
  }
  static int clear(lua_State* L) {
    const bool black = lua_toboolean(L, 1);
    drawing(L).renderer.clearScreen(black ? 0x00 : 0xFF);
    return 0;
  }
  static int pixel(lua_State* L) {
    auto& app = drawing(L);
    app.renderer.drawPixel(checkInt(L, 1), checkInt(L, 2), optBlack(L, 3));
    return 0;
  }
  // line(x1, y1, x2, y2 [, black [, width]])
  static int line(lua_State* L) {
    auto& app = drawing(L);
    const int lineWidth = static_cast<int>(luaL_optinteger(L, 6, 1));
    app.renderer.drawLine(checkInt(L, 1), checkInt(L, 2), checkInt(L, 3), checkInt(L, 4), lineWidth, optBlack(L, 5));
    return 0;
  }
  static int rect(lua_State* L) {
    auto& app = drawing(L);
    app.renderer.drawRect(checkInt(L, 1), checkInt(L, 2), checkInt(L, 3), checkInt(L, 4), optBlack(L, 5));
    return 0;
  }
  static int fillRect(lua_State* L) {
    auto& app = drawing(L);
    app.renderer.fillRectDither(checkInt(L, 1), checkInt(L, 2), checkInt(L, 3), checkInt(L, 4), optColor(L, 5));
    return 0;
  }
  // round_rect(x, y, w, h, radius [, black])
  static int roundRect(lua_State* L) {
    auto& app = drawing(L);
    app.renderer.drawRoundedRect(checkInt(L, 1), checkInt(L, 2), checkInt(L, 3), checkInt(L, 4), 1, checkInt(L, 5),
                                 optBlack(L, 6));
    return 0;
  }
  static int fillRoundRect(lua_State* L) {
    auto& app = drawing(L);
    app.renderer.fillRoundedRect(checkInt(L, 1), checkInt(L, 2), checkInt(L, 3), checkInt(L, 4), checkInt(L, 5),
                                 optColor(L, 6));
    return 0;
  }
  // circle(cx, cy, r [, black]): midpoint outline.
  static int circle(lua_State* L) {
    auto& app = drawing(L);
    const int cx = checkInt(L, 1);
    const int cy = checkInt(L, 2);
    const int r = checkInt(L, 3);
    const bool black = optBlack(L, 4);
    int x = r;
    int y = 0;
    int err = 1 - r;
    while (x >= y) {
      const int pts[8][2] = {{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}};
      for (const auto& p : pts) app.renderer.drawPixel(cx + p[0], cy + p[1], black);
      y++;
      if (err < 0) {
        err += 2 * y + 1;
      } else {
        x--;
        err += 2 * (y - x) + 1;
      }
    }
    return 0;
  }
  // fill_circle(cx, cy, r [, black]): horizontal spans.
  static int fillCircle(lua_State* L) {
    auto& app = drawing(L);
    const int cx = checkInt(L, 1);
    const int cy = checkInt(L, 2);
    const int r = checkInt(L, 3);
    const bool black = optBlack(L, 4);
    for (int dy = -r; dy <= r; dy++) {
      int dx = 0;
      while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
      app.renderer.fillRect(cx - dx, cy + dy, 2 * dx + 1, 1, black);
    }
    return 0;
  }
  // polygon({x1, y1, ...} [, black]): closed outline.
  static int polygon(lua_State* L) {
    auto& app = drawing(L);
    int* xs = nullptr;
    int* ys = nullptr;
    const int count = readPoints(L, 1, &xs, &ys);
    const bool black = optBlack(L, 2);
    for (int i = 0; i < count; i++) {
      const int j = (i + 1) % count;
      app.renderer.drawLine(xs[i], ys[i], xs[j], ys[j], black);
    }
    return 0;
  }
  static int fillPolygon(lua_State* L) {
    auto& app = drawing(L);
    int* xs = nullptr;
    int* ys = nullptr;
    const int count = readPoints(L, 1, &xs, &ys);
    app.renderer.fillPolygon(xs, ys, count, optBlack(L, 2));
    return 0;
  }
  // font(name [, style]): sets the font for text/text_width/line_height/wrap.
  static int font(lua_State* L) {
    auto& app = self(L);
    const int fontIndex = luaL_checkoption(L, 1, nullptr, FONT_NAMES);
    const int style = luaL_checkoption(L, 2, "regular", STYLE_NAMES);
    const bool reader = FONT_IDS[fontIndex] == READER_FONT_ID;
    app.fontId = reader ? SETTINGS.getReaderFontId() : FONT_IDS[fontIndex];
    app.lineCompression = reader ? SETTINGS.getReaderLineCompression() : 1.0f;
    app.fontStyle = static_cast<uint8_t>(style);
    return 0;
  }
  static EpdFontFamily::Style style(const LuaAppActivity& app) {
    return static_cast<EpdFontFamily::Style>(app.fontStyle);
  }
  // text(x, y, str [, black]): y is the top of the line box.
  static int text(lua_State* L) {
    auto& app = drawing(L);
    const int x = checkInt(L, 1);
    const int y = checkInt(L, 2);
    const char* str = luaL_checkstring(L, 3);
    app.renderer.drawText(app.fontId, x, y, str, optBlack(L, 4), style(app));
    return 0;
  }
  static int textWidth(lua_State* L) {
    auto& app = self(L);
    const char* str = luaL_checkstring(L, 1);
    lua_pushinteger(L, app.renderer.getTextWidth(app.fontId, str, style(app)));
    return 1;
  }
  static int lineHeight(lua_State* L) {
    auto& app = self(L);
    lua_pushinteger(L, app.renderer.getLineHeight(app.fontId, app.lineCompression));
    return 1;
  }
  // wrap(str, max_width [, max_lines]) -> { line, ... }
  static int wrap(lua_State* L) {
    auto& app = self(L);
    size_t len = 0;
    const char* str = luaL_checklstring(L, 1, &len);
    const int maxWidth = checkInt(L, 2);
    const int maxLines = static_cast<int>(luaL_optinteger(L, 3, MAX_WRAP_LINES));
    if (maxLines < 1 || maxLines > MAX_WRAP_LINES) luaL_argerror(L, 3, "out of range");
    // Output never exceeds the input plus an ellipsis and separator per line.
    const size_t capacity = len + static_cast<size_t>(maxLines) * 4 + 1;
    auto* buf = static_cast<char*>(lua_newuserdatauv(L, capacity, 0));
    int count = 0;
    {
      const auto lines = app.renderer.wrappedText(app.fontId, str, maxWidth, maxLines, style(app));
      size_t used = 0;
      for (const auto& line : lines) {
        if (used + line.size() + 1 > capacity) break;
        memcpy(buf + used, line.c_str(), line.size() + 1);
        used += line.size() + 1;
        count++;
      }
    }
    lua_createtable(L, count, 0);
    size_t offset = 0;
    for (int i = 1; i <= count; i++) {
      const size_t lineLen = strlen(buf + offset);
      lua_pushlstring(L, buf + offset, lineLen);
      lua_rawseti(L, -2, i);
      offset += lineLen + 1;
    }
    return 1;
  }
  // image(path, x, y [, max_w, max_h]) -> bool. 1/2/24-bit BMP, drawn in black and white.
  static int image(lua_State* L) {
    auto& app = drawing(L);
    char path[luapaths::MAX_PATH];
    LuaRuntime::checkPath(L, 1, path, sizeof(path));
    const int x = checkInt(L, 2);
    const int y = checkInt(L, 3);
    const int maxW = static_cast<int>(luaL_optinteger(L, 4, app.renderer.getScreenWidth() - x));
    const int maxH = static_cast<int>(luaL_optinteger(L, 5, app.renderer.getScreenHeight() - y));
    bool ok = false;
    {
      HalFile file;
      if (Storage.openFileForRead("LUA", path, file)) {
        Bitmap bitmap(file, true);
        ok = bitmap.parseHeaders() == BmpReaderError::Ok && app.renderer.drawBitmap(bitmap, x, y, maxW, maxH);
      }
    }
    lua_pushboolean(L, ok);
    return 1;
  }
  // photo(path, x, y [, w, h]) -> true | nil, err: a JPEG, PNG, BMP or .pxc in
  // four grey levels (see luaimage::draw for sizing). w and h default to the
  // rest of the screen.
  static int photo(lua_State* L) {
    auto& app = drawing(L);
    char path[luapaths::MAX_PATH];
    LuaRuntime::checkPath(L, 1, path, sizeof(path));
    const int x = checkInt(L, 2);
    const int y = checkInt(L, 3);
    const int w = static_cast<int>(luaL_optinteger(L, 4, app.renderer.getScreenWidth() - x));
    const int h = static_cast<int>(luaL_optinteger(L, 5, app.renderer.getScreenHeight() - y));
    if (!app.photos) {
      app.photos = makeUniqueNoThrow<LuaAppActivity::Photo[]>(LuaAppActivity::MAX_PHOTOS);
      if (!app.photos) luaL_error(L, "not enough memory");
    }
    if (app.photoCount >= LuaAppActivity::MAX_PHOTOS) {
      luaL_error(L, "at most %d photos per frame", static_cast<int>(LuaAppActivity::MAX_PHOTOS));
    }
    const char* err = luaimage::draw(app.renderer, path, x, y, w, h);
    if (err) {
      lua_pushnil(L);
      lua_pushstring(L, err);
      return 2;
    }
    auto& entry = app.photos[app.photoCount++];
    memcpy(entry.path, path, sizeof(entry.path));
    entry.x = static_cast<int16_t>(x);
    entry.y = static_cast<int16_t>(y);
    entry.width = static_cast<int16_t>(w);
    entry.height = static_cast<int16_t>(h);
    lua_pushboolean(L, 1);
    return 1;
  }
  // image_size(path) -> w, h | nil, err
  static int imageSize(lua_State* L) {
    char path[luapaths::MAX_PATH];
    LuaRuntime::checkPath(L, 1, path, sizeof(path));
    int w = 0;
    int h = 0;
    const char* err = luaimage::size(path, w, h);
    if (err) {
      lua_pushnil(L);
      lua_pushstring(L, err);
      return 2;
    }
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
  }
  // cache(src, dst, w, h) -> true | nil, err: decodes a JPEG or PNG to a w x h
  // .pxc that gfx.photo draws without decoding. Not inside draw(): it borrows
  // the framebuffer, and draw() already holds RenderLock.
  static int cache(lua_State* L) {
    auto& app = self(L);
    if (app.inDraw) luaL_error(L, "gfx.cache is not allowed inside draw()");
    char src[luapaths::MAX_PATH];
    char dst[luapaths::MAX_PATH];
    LuaRuntime::checkPath(L, 1, src, sizeof(src));
    LuaRuntime::checkPath(L, 2, dst, sizeof(dst), true);
    const int w = checkInt(L, 3);
    const int h = checkInt(L, 4);
    const char* err = "app stopped";
    // Give up on the lock once stopping, as drawFrame() does: onExit() holds it
    // while waiting for this task.
    while (!app.stopRequested.load()) {
      RenderLock lock(RenderLock::Mode::Try);
      if (!lock.ownsLock()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
      HalPowerManager::Lock powerLock;
      err = luaimage::writeCache(app.renderer, src, dst, w, h);
      break;
    }
    if (err) {
      lua_pushnil(L);
      lua_pushstring(L, err);
      return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
  }
  // hints(back, confirm, left, right) -> bar height: theme button-hint bar
  // along the bottom edge; nil hides a hint.
  static int hints(lua_State* L) {
    auto& app = drawing(L);
    const char* labels[4];
    for (int i = 0; i < 4; i++) labels[i] = luaL_optstring(L, i + 1, "");
    const auto mapped = app.mappedInput.mapLabels(labels[0], labels[1], labels[2], labels[3]);
    GUI.drawButtonHints(app.renderer, mapped.btn1, mapped.btn2, mapped.btn3, mapped.btn4);
    lua_pushinteger(L, UITheme::getInstance().getMetrics().buttonHintsHeight);
    return 1;
  }

  // --- sys -------------------------------------------------------------------
  // redraw([mode]): draw() runs once the current event is handled; mode is
  // "fast" (default), "half" or "full" (clears ghosting, slower).
  static int redraw(lua_State* L) {
    auto& app = self(L);
    const int mode = luaL_checkoption(L, 1, "fast", REFRESH_NAMES);
    app.redrawRequested = true;
    // Keep the strongest refresh asked for since the last frame.
    if (REFRESH_MODES[mode] < app.refreshMode) app.refreshMode = REFRESH_MODES[mode];
    return 0;
  }
  static int sysExit(lua_State* L) {
    self(L).exitRequested.store(true);
    return 0;
  }
  static int millisFn(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(millis() & 0x7FFFFFFF));
    return 1;
  }
  // set_tick(ms): call on_tick() every ms (>= 250); 0 stops.
  static int setTick(lua_State* L) {
    auto& app = self(L);
    const lua_Integer ms = luaL_checkinteger(L, 1);
    if (ms < 0) luaL_argerror(L, 1, "must be >= 0");
    const uint32_t interval =
        ms == 0 ? 0 : static_cast<uint32_t>(ms < LuaAppActivity::MIN_TICK_MS ? LuaAppActivity::MIN_TICK_MS : ms);
    app.tickIntervalMs.store(interval);
    return 0;
  }
  // keyboard(title, initial, callback [, max_length]): opens the on-screen
  // keyboard; callback(text) runs on confirm, callback(nil) on cancel.
  static int keyboard(lua_State* L) {
    auto& app = self(L);
    size_t titleLen = 0;
    size_t textLen = 0;
    const char* title = luaL_checklstring(L, 1, &titleLen);
    const char* initial = luaL_optlstring(L, 2, "", &textLen);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    const lua_Integer maxLength = luaL_optinteger(L, 4, MAX_KEYBOARD_TEXT);
    if (maxLength < 1 || maxLength > static_cast<lua_Integer>(MAX_KEYBOARD_TEXT)) luaL_argerror(L, 4, "out of range");
    if (textLen > static_cast<size_t>(maxLength)) luaL_argerror(L, 2, "longer than max_length");
    if (app.keyboardCallbackRef != LUA_NOREF || app.keyboardRequested.load()) luaL_error(L, "keyboard already open");
    lua_pushvalue(L, 3);
    app.keyboardCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    app.keyboardTitle.assign(title, titleLen);
    app.keyboardText.assign(initial, textLen);
    app.keyboardMaxLength = static_cast<size_t>(maxLength);
    app.keyboardRequested.store(true);
    return 0;
  }
  static int heap(lua_State* L) {
    lua_pushinteger(L, static_cast<lua_Integer>(LuaRuntime::from(L)->heapUsed()));
    lua_pushinteger(L, static_cast<lua_Integer>(LuaRuntime::HEAP_CAP));
    return 2;
  }
  static int hasTouch(lua_State* L) {
    lua_pushboolean(L, self(L).mappedInput.hasTouch());
    return 1;
  }
  static int appDir(lua_State* L) {
    lua_pushstring(L, LuaRuntime::from(L)->appDir());
    return 1;
  }
  // keep_awake(on): while on, inactivity does not put the device to sleep.
  static int keepAwake(lua_State* L) {
    self(L).keepAwakeRequested.store(lua_toboolean(L, 1));
    return 0;
  }
  // light_sleep(on): while on and no event is pending, the chip light-sleeps
  // between input polls once the device has been idle for a few seconds.
  static int lightSleep(lua_State* L) {
    self(L).lightSleepRequested.store(lua_toboolean(L, 1));
    return 0;
  }

  static void registerModules(lua_State* L) {
    static constexpr luaL_Reg GFX[] = {{"width", width},
                                       {"height", height},
                                       {"clear", clear},
                                       {"pixel", pixel},
                                       {"line", line},
                                       {"rect", rect},
                                       {"fill_rect", fillRect},
                                       {"round_rect", roundRect},
                                       {"fill_round_rect", fillRoundRect},
                                       {"circle", circle},
                                       {"fill_circle", fillCircle},
                                       {"polygon", polygon},
                                       {"fill_polygon", fillPolygon},
                                       {"font", font},
                                       {"text", text},
                                       {"text_width", textWidth},
                                       {"line_height", lineHeight},
                                       {"wrap", wrap},
                                       {"image", image},
                                       {"photo", photo},
                                       {"image_size", imageSize},
                                       {"cache", cache},
                                       {"hints", hints},
                                       {nullptr, nullptr}};
    static constexpr luaL_Reg SYS[] = {{"redraw", redraw},          {"exit", sysExit},      {"millis", millisFn},
                                       {"set_tick", setTick},       {"keyboard", keyboard}, {"heap", heap},
                                       {"has_touch", hasTouch},     {"app_dir", appDir},    {"keep_awake", keepAwake},
                                       {"light_sleep", lightSleep}, {nullptr, nullptr}};
    luaL_newlib(L, GFX);
    lua_setglobal(L, "gfx");
    luaL_newlib(L, SYS);
    lua_setglobal(L, "sys");
  }
};

LuaAppActivity::LuaAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string scriptPath,
                               std::string appName, std::string hookFunction, std::string hookWord)
    : Activity("LuaApp", renderer, mappedInput),
      scriptPath(std::move(scriptPath)),
      appName(std::move(appName)),
      hookFunction(std::move(hookFunction)),
      hookWord(std::move(hookWord)) {}

void LuaAppActivity::onEnter() {
  Activity::onEnter();
  fontId = UI_10_FONT_ID;
  lineCompression = 1.0f;
  stopRequested.store(false);
  queue = xQueueCreate(QUEUE_LENGTH, sizeof(Event));
  taskDone = xSemaphoreCreateBinary();
  // The task owns the Lua state for the activity's lifetime; its stack is
  // heap-allocated here and released in onExit.
  if (!queue || !taskDone ||
      xTaskCreate(&LuaAppActivity::taskTrampoline, "LuaApp", TASK_STACK_BYTES, this, 1, &task) != pdPASS) {
    task = nullptr;
    LOG_ERR("LUA", "OOM: app task (free internal heap %u)",
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    snprintf(errorText, sizeof(errorText), "not enough memory to start the app");
    failed.store(true);
    renderer.clearScreen();
    drawError();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }
  postEvent({EventType::Start, 0, 0, 0});
}

void LuaAppActivity::onExit() {
  // Runs with RenderLock held: the task gives up on the lock once it sees
  // stopRequested, and the count hook aborts any running Lua.
  stopRequested.store(true);
  if (task) {
    static constexpr TickType_t STOP_TIMEOUT = pdMS_TO_TICKS(5000);
    if (xSemaphoreTake(taskDone, STOP_TIMEOUT) != pdTRUE) {
      // Only reachable if a single native call (SD I/O, a panel refresh) blocks
      // for seconds; deleting the task then leaks its Lua heap but keeps the
      // device usable.
      LOG_ERR("LUA", "App task did not stop; deleting it");
      vTaskDelete(task);
      runtime.abandon();
    }
    task = nullptr;
  }
  if (queue) {
    vQueueDelete(queue);
    queue = nullptr;
  }
  if (taskDone) {
    vSemaphoreDelete(taskDone);
    taskDone = nullptr;
  }
  Activity::onExit();
}

bool LuaAppActivity::postEvent(const Event& event) {
  if (!queue) return false;
  pendingEvents.fetch_add(1);
  if (xQueueSend(queue, &event, 0) == pdTRUE) return true;
  pendingEvents.fetch_sub(1);
  return false;
}

bool LuaAppActivity::allowIdleLightSleep() {
  // Touch controllers are polled for their current state, so a tap shorter
  // than a light-sleep slice would be missed.
  return lightSleepRequested.load() && pendingEvents.load() == 0 && !keyboardRequested.load() &&
         !mappedInput.hasTouch();
}

void LuaAppActivity::loop() {
  if (exitRequested.load()) {
    finish();
    return;
  }
  // Escape hatch that works even when the app ignores Back or is stuck.
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Back, EXIT_HOLD_MS)) {
    finish();
    return;
  }
  if (failed.load()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasBackGesture()) {
      finish();
    }
    return;
  }

  if (keyboardRequested.load()) {
    auto keyboard =
        makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, keyboardTitle, keyboardText, keyboardMaxLength);
    keyboardRequested.store(false);
    if (!keyboard) {
      LOG_ERR("LUA", "OOM: keyboard activity");
      keyboardCancelled = true;
      postEvent({EventType::Keyboard, 0, 0, 0});
      return;
    }
    startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
      const auto* text = std::get_if<KeyboardResult>(&result.data);
      keyboardCancelled = result.isCancelled || text == nullptr;
      if (!keyboardCancelled) keyboardText = text->text;
      postEvent({EventType::Keyboard, 0, 0, 0});
    });
    return;
  }

  // wasReleased(Back) already counts the back gesture.
  for (size_t i = 0; i < static_cast<size_t>(AppButton::Count); i++) {
    if (mappedInput.wasReleased(BUTTONS[i])) postEvent({EventType::Button, static_cast<uint8_t>(i), 0, 0});
  }
  if (!mappedInput.wasBackGesture() && !mappedInput.wasHomeGesture() && !mappedInput.wasLightPanelGesture()) {
    const auto swipe = mappedInput.wasSwipe();
    for (size_t i = 0; i < sizeof(SWIPES) / sizeof(SWIPES[0]); i++) {
      if (swipe == SWIPES[i]) postEvent({EventType::Swipe, static_cast<uint8_t>(i), 0, 0});
    }
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasScreenLongPress(x, y)) {
    postEvent({EventType::Touch, static_cast<uint8_t>(TouchKind::LongPress), static_cast<int16_t>(x),
               static_cast<int16_t>(y)});
  } else if (mappedInput.wasScreenTapped(x, y)) {
    postEvent(
        {EventType::Touch, static_cast<uint8_t>(TouchKind::Tap), static_cast<int16_t>(x), static_cast<int16_t>(y)});
  }

  const uint32_t interval = tickIntervalMs.load();
  const unsigned long now = millis();
  if (interval > 0 && !tickPending.load() && now - lastTickMs >= interval) {
    lastTickMs = now;
    tickPending.store(true);
    if (!postEvent({EventType::Tick, 0, 0, 0})) tickPending.store(false);
  }
}

void LuaAppActivity::render(RenderLock&&) { postEvent({EventType::Redraw, 0, 0, 0}); }

void LuaAppActivity::taskTrampoline(void* param) { static_cast<LuaAppActivity*>(param)->taskMain(); }

void LuaAppActivity::taskMain() {
  while (!stopRequested.load()) {
    Event event{};
    if (xQueueReceive(queue, &event, pdMS_TO_TICKS(100)) != pdTRUE) continue;
    handleEvent(event);
    pendingEvents.fetch_sub(1);
  }
  runtime.close();
  xSemaphoreGive(taskDone);
  vTaskDelete(nullptr);
}

void LuaAppActivity::handleEvent(const Event& event) {
  if (event.type == EventType::Redraw) {
    redrawRequested = true;
  } else if (!failed.load()) {
    bool ok;
    if (event.type == EventType::Start) {
      ok = runtime.open(appName.c_str(), &stopRequested, this, errorText, sizeof(errorText)) &&
           runtime.call(&LuaAppActivity::protectedStart, this, errorText, sizeof(errorText));
      redrawRequested = true;
    } else {
      if (event.type == EventType::Tick) tickPending.store(false);
      EventCall call{this, &event};
      ok = runtime.call(&LuaAppActivity::protectedEvent, &call, errorText, sizeof(errorText));
    }
    if (!ok) fail();
  }
  // Coalesce: draw once the burst of queued events has been handled.
  if (redrawRequested && !exitRequested.load() && uxQueueMessagesWaiting(queue) == 0) drawFrame();
}

void LuaAppActivity::fail() {
  if (stopRequested.load()) return;  // "app stopped" is the expected exit path
  LOG_ERR("LUA", "%s: %s", appName.c_str(), errorText);
  failed.store(true);
  tickIntervalMs.store(0);
  keepAwakeRequested.store(false);
  lightSleepRequested.store(false);
  redrawRequested = true;
  refreshMode = HalDisplay::HALF_REFRESH;
}

int LuaAppActivity::protectedStart(lua_State* L) {
  auto* self = static_cast<LuaAppActivity*>(lua_touserdata(L, 1));
  LuaAppBindings::registerModules(L);
  // Error messages cite "<app>.lua:<line>".
  lua_pushfstring(L, "@%s/%s", self->appName.c_str(), luapaths::ENTRY_FILE);
  LuaRuntime::loadFile(L, self->scriptPath.c_str(), lua_tostring(L, -1));
  lua_call(L, 0, 0);
  if (!self->hookFunction.empty()) {
    if (lua_getglobal(L, self->hookFunction.c_str()) != LUA_TFUNCTION) {
      luaL_error(L, "lookup hook '%s' is not a global function", self->hookFunction.c_str());
    }
    lua_pushlstring(L, self->hookWord.data(), self->hookWord.size());
    lua_call(L, 1, 0);
  }
  return 0;
}

int LuaAppActivity::protectedEvent(lua_State* L) {
  const auto* call = static_cast<EventCall*>(lua_touserdata(L, 1));
  LuaAppActivity* self = call->self;
  const auto& event = *static_cast<const Event*>(call->event);

  if (event.type == EventType::Keyboard) {
    const int ref = self->keyboardCallbackRef;
    self->keyboardCallbackRef = LUA_NOREF;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    luaL_unref(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) return 0;
    if (self->keyboardCancelled) {
      lua_pushnil(L);
    } else {
      lua_pushlstring(L, self->keyboardText.data(), self->keyboardText.size());
    }
    lua_call(L, 1, 0);
    return 0;
  }

  const char* callback = event.type == EventType::Button  ? "on_button"
                         : event.type == EventType::Touch ? "on_touch"
                         : event.type == EventType::Swipe ? "on_swipe"
                                                          : "on_tick";
  const bool isBack = event.type == EventType::Button && event.detail == static_cast<uint8_t>(AppButton::Back);
  if (lua_getglobal(L, callback) != LUA_TFUNCTION) {
    // Without a handler, Back leaves the app.
    if (isBack) self->exitRequested.store(true);
    return 0;
  }
  int nargs = 0;
  if (event.type == EventType::Button) {
    lua_pushstring(L, BUTTON_NAMES[event.detail]);
    nargs = 1;
  } else if (event.type == EventType::Touch) {
    lua_pushinteger(L, event.x);
    lua_pushinteger(L, event.y);
    lua_pushstring(L, event.detail == static_cast<uint8_t>(TouchKind::LongPress) ? "long" : "tap");
    nargs = 3;
  } else if (event.type == EventType::Swipe) {
    lua_pushstring(L, SWIPE_NAMES[event.detail]);
    nargs = 1;
  }
  lua_call(L, nargs, 1);
  // on_button("back") returning true keeps the app open (e.g. to close a
  // dialog); anything else exits.
  if (isBack && !lua_toboolean(L, -1)) self->exitRequested.store(true);
  return 0;
}

int LuaAppActivity::protectedDraw(lua_State* L) {
  if (lua_getglobal(L, "draw") != LUA_TFUNCTION) return 0;
  lua_call(L, 0, 0);
  return 0;
}

void LuaAppActivity::drawFrame() {
  while (!stopRequested.load()) {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock()) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    redrawRequested = false;
    // A pushed screen (keyboard, light panel) owns the framebuffer; the
    // manager re-renders this activity when it pops, which posts Redraw.
    if (!activityManager.isCurrent(this)) return;

    // Full clock for the render even when the idle loop has downclocked.
    HalPowerManager::Lock powerLock;
    renderer.clearScreen();
    photoCount = 0;
    if (!failed.load()) {
      inDraw = true;
      const bool ok = runtime.call(&LuaAppActivity::protectedDraw, nullptr, errorText, sizeof(errorText));
      inDraw = false;
      if (!ok) {
        fail();
        redrawRequested = false;
        renderer.clearScreen();
      }
    }
    if (failed.load()) {
      photoCount = 0;
      drawError();
    }
    if (photoCount > 0) {
      displayWithPhotos();
    } else {
      renderer.displayBuffer(refreshMode);
    }
    refreshMode = HalDisplay::FAST_REFRESH;
    return;
  }
}

void LuaAppActivity::displayWithPhotos() {
  // The photos drew every non-white pixel black; the planes then mark which of
  // those are grey. The stored BW frame is restored afterwards so the next
  // differential refresh starts from what the panel shows.
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("LUA", "No memory to store the BW frame; showing photos in black and white");
    renderer.displayBuffer(refreshMode);
    return;
  }
  renderer.displayGrayscaleBase(refreshMode);
  for (const auto plane : {GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    renderer.clearScreen(0x00);
    renderer.setRenderMode(plane);
    for (uint8_t i = 0; i < photoCount; i++) {
      const Photo& photo = photos[i];
      if (const char* err = luaimage::draw(renderer, photo.path, photo.x, photo.y, photo.width, photo.height)) {
        LOG_ERR("LUA", "Grey pass of %s: %s", photo.path, err);
      }
    }
    if (plane == GfxRenderer::GRAYSCALE_LSB) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
  }
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.restoreBwBuffer();
  photoCount = 0;
}

void LuaAppActivity::drawError() {
  const int width = renderer.getScreenWidth();
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int margin = left + 16;
  int y = top + 24;
  renderer.drawText(UI_12_FONT_ID, margin, y, tr(STR_LUA_APP_ERROR), true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 8;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, errorText, width - margin - right - 16, 12)) {
    renderer.drawText(UI_10_FONT_ID, margin, y, line.c_str());
    y += lineHeight;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
