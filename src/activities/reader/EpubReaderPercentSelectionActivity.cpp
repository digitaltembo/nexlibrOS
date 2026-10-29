#include "EpubReaderPercentSelectionActivity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "components/UITheme.h"
#include "components/UiSliderDialog.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId ACTION_SLIDER = 1;
constexpr fui::ActionId ACTION_STEP = 2;
constexpr fui::ActionId ACTION_OK = 4;
constexpr fui::ActionId ACTION_CHROME = 5;  // absorbs taps on the dialog body; no handler
constexpr fui::ActionId ACTION_KEY = 6;     // number pad: value = digit, or one of the keys below
constexpr int16_t KEY_DELETE = 10;
constexpr int16_t KEY_OK = 11;
constexpr int kKeypadRows = 4;
constexpr int kKeypadCols = 3;
constexpr int16_t kKeyHeight = 56;
constexpr int16_t kKeyGap = 8;
constexpr int kMaxEntryDigits = 6;
// Fine/coarse step sizes for percent adjustments (buttons and -/+ tap zones).
constexpr int kSmallStep = 1;
constexpr int kLargeStep = 10;
}  // namespace

EpubReaderPercentSelectionActivity::EpubReaderPercentSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const int initialPercent)
    : Activity("EpubReaderPercentSelection", renderer, mappedInput), UiAppHost(renderer), percent(initialPercent) {}

EpubReaderPercentSelectionActivity::EpubReaderPercentSelectionActivity(GfxRenderer& renderer,
                                                                       MappedInputManager& mappedInput,
                                                                       const int initialPage, const int totalPages)
    : Activity("EpubReaderPercentSelection", renderer, mappedInput),
      UiAppHost(renderer),
      percent(std::clamp(initialPage, 1, std::max(1, totalPages))),
      pageMode(true),
      minValue(1),
      maxValue(std::max(1, totalPages)) {}

void EpubReaderPercentSelectionActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_SLIDER, &EpubReaderPercentSelectionActivity::onSliderEvent, this);
  app.on(ACTION_STEP, &EpubReaderPercentSelectionActivity::onStepEvent, this);
  app.on(ACTION_OK, &EpubReaderPercentSelectionActivity::onOkEvent, this);
  app.on(ACTION_KEY, &EpubReaderPercentSelectionActivity::onKeyEvent, this);
  app.setScreen(&EpubReaderPercentSelectionActivity::percentScreen, this);
  // Set up rendering task and mark first frame dirty.
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderPercentSelectionActivity::adjustPercent(const int delta) {
  entryLength = 0;  // a step edits the shown value, not the typed digits
  entry[0] = '\0';
  if (pageMode) {
    // Pages clamp at the ends: wrapping from page 1 to the last page is never what a step means.
    setPercent(percent + delta);
    return;
  }
  // Wrap using a 100-value ring (0% and 100% are the same wrap point), but keep 100 as the
  // natural landing value when reached without crossing the boundary (e.g. 90 + 10 = 100).
  const int raw = percent + delta;
  if (raw > 0 && raw % 100 == 0) {
    percent = 100;
  } else {
    percent = ((raw % 100) + 100) % 100;
  }
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::setPercent(const int value) {
  const int clamped = std::clamp(value, minValue, maxValue);
  if (clamped == percent) return;
  percent = clamped;
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::onSliderEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  if (event.dragPermille < 0) return;
  self->setPercent(self->minValue +
                   (static_cast<int>(event.dragPermille) * (self->maxValue - self->minValue) + 500) / 1000);
}

void EpubReaderPercentSelectionActivity::onStepEvent(const fui::ActionEvent& event, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->adjustPercent(event.value * kSmallStep);
}

void EpubReaderPercentSelectionActivity::onOkEvent(const fui::ActionEvent&, void* user) {
  auto* self = static_cast<EpubReaderPercentSelectionActivity*>(user);
  self->app.clearTapFlash();  // the tap leaves this screen
  self->confirm();
}

void EpubReaderPercentSelectionActivity::onKeyEvent(const fui::ActionEvent& event, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->pressKey(event.value);
}

void EpubReaderPercentSelectionActivity::pressKey(const int key) {
  if (key == KEY_OK) {
    if (entryLength > 0) {
      const int typed = atoi(entry);
      if (typed < minValue) return;  // e.g. page 0: keep the pad up
      percent = typed;
    }
    app.clearTapFlash();  // the tap leaves this screen
    confirm();
    return;
  }
  if (key == KEY_DELETE) {
    if (entryLength > 0) entry[--entryLength] = '\0';
    requestUpdate();
    return;
  }
  if (key < 0 || key > 9 || entryLength >= kMaxEntryDigits) return;
  // A lone leading zero is replaced, not extended.
  if (entryLength == 1 && entry[0] == '0') entryLength = 0;
  const int candidate = (entryLength > 0 ? atoi(entry) : 0) * 10 + key;
  if (candidate > maxValue) return;  // past the end: ignore the digit
  entry[entryLength++] = static_cast<char>('0' + key);
  entry[entryLength] = '\0';
  requestUpdate();
}

void EpubReaderPercentSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void EpubReaderPercentSelectionActivity::confirm() {
  if (pageMode) {
    setResult(PageResult{static_cast<uint32_t>(percent)});
  } else {
    setResult(PercentResult{percent});
  }
  finish();
}

void EpubReaderPercentSelectionActivity::loop() {
  // Touch goes through the FreeInkApp: render() registered the slider and -/+ hit
  // rects; the slider follows the finger via InputDrag (dragPermille per held frame).
  // Runs before the Back handler because the release of a drag can also register as a
  // swipe (e.g. the left-edge rightward back gesture) — the drag must consume it so it
  // can't cancel the dialog or step the percent.
  const auto route = routeTouch(mappedInput, false, /*routeHeld=*/true);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) {
    if (route.event.dragPermille >= 0) draggingSlider = true;
    return;
  }
  if (routingReady() && draggingSlider) {
    // Drag ended (possibly off the slider): swallow the tap/swipe events it produced.
    if (!route.snap.touchHeld) draggingSlider = false;
    return;
  }
  // Tap released outside the dialog (inside-taps are absorbed by the chrome
  // guard): cancel. Swipe-end releases arrive with -1,-1 coords and fall
  // through — same rule as OptionPopup.
  if (route.routed && route.snap.touchReleased && route.snap.touchX >= 0) {
    cancel();
    return;
  }

  // Back cancels, confirm selects, arrows adjust the percent.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  const auto swipe = mappedInput.hasTouch() ? MappedInputManager::SwipeDir::None : mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Right) {
    adjustPercent(kLargeStep);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Left) {
    adjustPercent(-kLargeStep);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirm();
    return;
  }

  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Left}, [this] { adjustPercent(-kSmallStep); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Right}, [this] { adjustPercent(kSmallStep); });

  // On edge-button boards (X3, X4 Pro) the side buttons sit on the left/right edges of the screen rather
  // than as a vertical up/down rocker (X4), so BTN_UP is physically the left button and BTN_DOWN the right
  // one. Flip the large-step direction there so the left button decreases and the right button increases.
  const int upDelta = gpio.hasEdgeSideButtons() ? -kLargeStep : kLargeStep;
  const int downDelta = gpio.hasEdgeSideButtons() ? kLargeStep : -kLargeStep;
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Up}, [this, upDelta] { adjustPercent(upDelta); });
  buttonNavigator.onPressAndContinuous({MappedInputManager::Button::Down},
                                       [this, downDelta] { adjustPercent(downDelta); });
}

void EpubReaderPercentSelectionActivity::percentScreen(UiScreen& screen, void* user) {
  static_cast<EpubReaderPercentSelectionActivity*>(user)->buildPercentScreen(screen);
}

void EpubReaderPercentSelectionActivity::buildPercentScreen(UiScreen& screen) {
  if (mappedInput.hasTouch()) {
    buildKeypadScreen(screen);
    return;
  }
  // Page mode drops the % unit everywhere.
  const char* unit = pageMode ? "" : "%";
  char readout[24];
  if (pageMode) {
    snprintf(readout, sizeof(readout), "%d / %d", percent, maxValue);
  } else {
    snprintf(readout, sizeof(readout), "%d%%", percent);
  }
  char hint1[64];
  snprintf(hint1, sizeof(hint1), "%s %d%s", I18N.get(StrId::STR_STEP_HINT_FRONT), kSmallStep, unit);
  char hint2[64];
  snprintf(hint2, sizeof(hint2), "%s %d%s", I18N.get(StrId::STR_STEP_HINT_SIDE), kLargeStep, unit);
  char minLabel[12];
  snprintf(minLabel, sizeof(minLabel), "%d%s", minValue, unit);
  char maxLabel[12];
  snprintf(maxLabel, sizeof(maxLabel), "%d%s", maxValue, unit);

  UiSliderDialogSpec spec;
  spec.title = pageMode ? tr(STR_GO_TO_PAGE) : tr(STR_GO_TO_PERCENT);
  spec.readout = readout;
  spec.value = percent - minValue;
  spec.max = std::max(1, maxValue - minValue);
  spec.minLabel = minLabel;
  spec.maxLabel = maxLabel;
  spec.sliderAction = ACTION_SLIDER;
  spec.stepAction = ACTION_STEP;
  spec.okAction = ACTION_OK;
  spec.chromeAction = ACTION_CHROME;
  spec.hintLine1 = hint1;
  spec.hintLine2 = hint2;
  buildSliderDialogScreen(screen, uiTarget, mappedInput, spec);
}

// Popup with the value as the headline, the valid range under it, and a
// 1-9 / delete-0-OK number pad. Typing replaces the shown value; OK jumps.
void EpubReaderPercentSelectionActivity::buildKeypadScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  // The readout is the dialog's hero, as in the slider dialog.
  uiTarget.setFont(fui::GfxRendererTarget::FONT_TITLE, NOTOSANS_18_FONT_ID);

  const int value = entryLength > 0 ? atoi(entry) : percent;
  char readout[24];
  if (pageMode) {
    snprintf(readout, sizeof(readout), "%d / %d", value, maxValue);
  } else {
    snprintf(readout, sizeof(readout), "%d%%", value);
  }
  char range[32];
  snprintf(range, sizeof(range), pageMode ? "%d - %d" : "%d%% - %d%%", minValue, maxValue);

  static constexpr int16_t kKeyOrder[kKeypadRows * kKeypadCols] = {1, 2, 3, 4, 5, 6, 7, 8, 9, KEY_DELETE, 0, KEY_OK};
  static const char* const kDigitLabels[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  for (int i = 0; i < kKeypadRows * kKeypadCols; ++i) {
    fui::KeyGridKey key;
    key.value = kKeyOrder[i];
    if (kKeyOrder[i] == KEY_DELETE) {
      key.kind = fui::KeyKind::Delete;
      key.enabled = entryLength > 0;
    } else if (kKeyOrder[i] == KEY_OK) {
      key.kind = fui::KeyKind::Ok;
      key.label = tr(STR_OK_BUTTON);
    } else {
      key.label = kDigitLabels[kKeyOrder[i]];
    }
    keypadKeys[i] = key;
  }

  // Same panel chrome as the slider dialog / OptionPopup. Static: the props
  // embed StyleSets, well past the 256-byte local stack budget.
  static fui::OptionDialogProps props;
  props.title = pageMode ? tr(STR_GO_TO_PAGE) : tr(STR_GO_TO_PERCENT);
  props.headline = readout;
  props.message = range;
  props.options = nullptr;
  props.optionCount = 0;
  props.inputMask = fui::InputTouch;
  props.titleText = theme.smallText;
  props.titleText.bold = true;
  props.titleText.align = fui::TextAlign::Center;
  props.headlineText = theme.titleText;
  props.headlineText.bold = true;
  props.headlineText.align = fui::TextAlign::Center;
  props.messageText = theme.smallText;
  props.messageText.align = fui::TextAlign::Center;
  const int16_t innerPadding = static_cast<int16_t>(metrics.optionPopupInnerPadding);
  props.padding = fui::Insets{innerPadding, innerPadding, innerPadding, innerPadding};
  props.gap = static_cast<int16_t>(metrics.optionPopupItemSpacing);
  props.styles = fui::defaultPopupStyles();
  props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = props.styles.normal;
  props.styles.focused = props.styles.normal;
  props.styles.active = props.styles.normal;
  props.styles.disabled = props.styles.normal;
  props.contentHeight = static_cast<int16_t>(kKeypadRows * kKeyHeight + (kKeypadRows - 1) * kKeyGap);

  const fui::Rect screenRect = screen.frame().screen();
  const int16_t width = fui::clampI16(
      std::min<int>(screenRect.width * 3 / 4, screenRect.width - metrics.optionPopupDialogSideMargin * 2));
  const int16_t height = fui::clampI16(fui::optionDialogHeight(screen.target(), props, width), 0, screenRect.height);
  const fui::Rect dialog = fui::centeredRect(screenRect, fui::Size{width, height});
  // Chrome guard first, keys after: routing scans newest-first, so the keys
  // win inside the dialog and the guard absorbs the rest.
  screen.frame().hit(dialog, ACTION_CHROME, 0, fui::InputTouch);
  const fui::Rect band = fui::optionDialog(screen.frame(), dialog, props);

  fui::KeyGridProps grid;
  grid.keys = keypadKeys;
  grid.rows = kKeypadRows;
  grid.cols = kKeypadCols;
  grid.action = ACTION_KEY;
  grid.inputMask = fui::InputTouch;
  grid.labelText = theme.bodyText;
  grid.labelText.align = fui::TextAlign::Center;
  grid.gap = kKeyGap;
  grid.radius = theme.controlRadius;
  fui::keyGrid(screen.frame(), band, grid);
}

void EpubReaderPercentSelectionActivity::render(RenderLock&&) {
  // No clearScreen: the dialog is a popup — it dims the frame it opened over
  // and draws the card on top. Everything renders through the app so the
  // slider, -/+ zones, and Cancel/OK register touch hit rects.
  renderUi();

  // Button hints follow the current front button layout.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
