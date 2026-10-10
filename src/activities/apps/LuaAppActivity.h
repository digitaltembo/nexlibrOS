#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "lua/LuaRuntime.h"

// Runs one Lua app from /apps/<name>/index.lua. See docs/lua-apps.md for the app API.
//
// All Lua executes on a dedicated task that owns the LuaRuntime: the parser
// and C-call chains need more stack than the 8KB loop/render tasks have, and
// a long-running script must not stall input, sleep, or Home handling.
// loop() turns input into events for that task; the task calls the app's
// callbacks and, when a redraw is pending, renders the app's draw() into the
// framebuffer and pushes it to the panel itself under RenderLock.
class LuaAppActivity final : public Activity {
 public:
  LuaAppActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string scriptPath, std::string appName);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // A screen pushed on top of the app (keyboard, light panel) overwrote the
  // framebuffer; the manager re-renders on return, so redraw the app.
  void render(RenderLock&&) override;

 private:
  friend struct LuaAppBindings;

  enum class EventType : uint8_t { Start, Button, Touch, Swipe, Tick, Keyboard, Redraw };
  // Lua names indexed by the Button field of an event.
  enum class AppButton : uint8_t { Back, Confirm, Left, Right, Up, Down, Count };
  enum class TouchKind : uint8_t { Tap, LongPress };
  struct Event {
    EventType type;
    uint8_t detail;  // AppButton, TouchKind, or SWIPE_NAMES index
    int16_t x;
    int16_t y;
  };

  static constexpr uint32_t TASK_STACK_BYTES = 16 * 1024;
  static constexpr UBaseType_t QUEUE_LENGTH = 8;
  static constexpr uint32_t MIN_TICK_MS = 250;
  static constexpr unsigned long EXIT_HOLD_MS = 1000;
  static constexpr size_t ERROR_TEXT_BYTES = 256;

  static void taskTrampoline(void* param);
  void taskMain();
  bool postEvent(const Event& event);
  // Task side: dispatch one event to the app's callback, then redraw if asked.
  void handleEvent(const Event& event);
  // Waits for RenderLock (giving up once stopping: onExit runs with the lock
  // held and waits for this task), then renders draw() and refreshes.
  void drawFrame();
  // Paints the error screen into the framebuffer; caller refreshes.
  void drawError();
  void fail();
  static int protectedStart(lua_State* L);
  static int protectedEvent(lua_State* L);
  static int protectedDraw(lua_State* L);

  std::string scriptPath;
  std::string appName;

  LuaRuntime runtime;
  TaskHandle_t task = nullptr;
  QueueHandle_t queue = nullptr;
  SemaphoreHandle_t taskDone = nullptr;
  std::atomic<bool> stopRequested{false};

  // Written by the task, consumed by loop().
  std::atomic<bool> exitRequested{false};
  std::atomic<bool> failed{false};
  std::atomic<bool> keyboardRequested{false};
  std::atomic<uint32_t> tickIntervalMs{0};
  std::atomic<bool> tickPending{false};
  unsigned long lastTickMs = 0;

  // Task-only state.
  bool redrawRequested = false;
  HalDisplay::RefreshMode refreshMode = HalDisplay::FAST_REFRESH;
  bool inDraw = false;
  int fontId = 0;
  uint8_t fontStyle = 0;
  int keyboardCallbackRef = -2;  // LUA_NOREF
  char errorText[ERROR_TEXT_BYTES] = {};

  // Keyboard request (task -> loop) and result (result handler -> task). The
  // task writes the request before setting keyboardRequested and does not
  // touch these again until the Keyboard event arrives.
  std::string keyboardTitle;
  std::string keyboardText;
  size_t keyboardMaxLength = 0;
  bool keyboardCancelled = false;
};
