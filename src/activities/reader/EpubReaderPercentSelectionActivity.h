#pragma once

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class EpubReaderPercentSelectionActivity final : public Activity, private UiAppHost {
 public:
  // Slider-style percent selector for jumping within a book.
  explicit EpubReaderPercentSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              int initialPercent);
  // Same dialog over book pages 1..totalPages; finishes with a PageResult.
  explicit EpubReaderPercentSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int initialPage,
                                              int totalPages);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // The UiAppHost app hosts the shared slider dialog popup (capsule slider,
  // [-] [+] [Confirm] buttons) drawn over the screen underneath.
  static void percentScreen(UiScreen& screen, void* user);
  static void onSliderEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onStepEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onOkEvent(const freeink::ui::ActionEvent& event, void* user);
  static void onKeyEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildPercentScreen(UiScreen& screen);
  // Touch boards type the value on a number pad instead of sliding to it.
  void buildKeypadScreen(UiScreen& screen);
  void pressKey(int key);
  void cancel();
  void confirm();

  // Current value shown on the slider: a percent (0-100), or a book page
  // (1..maxValue) in page mode.
  int percent = 0;
  bool pageMode = false;
  int minValue = 0;
  int maxValue = 100;
  // Digits typed on the number pad; empty shows the current value.
  char entry[8] = {};
  uint8_t entryLength = 0;
  // Number pad keys (KeyGridKey is ~40 bytes; twelve stay off the stack).
  freeink::ui::KeyGridKey keypadKeys[12];

  ButtonNavigator buttonNavigator;

  // Swallow the swipe/tap fallout of a slider drag so its release can't trigger
  // the back gesture and cancel the dialog, or step the percent as a swipe.
  bool draggingSlider = false;

  // Change the current percent by a delta and wrap within bounds.
  void adjustPercent(int delta);
  // Absolute value (clamped to the range), from slider drag/tap positions.
  void setPercent(int value);
};
