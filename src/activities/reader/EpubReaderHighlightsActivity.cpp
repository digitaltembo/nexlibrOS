#include "EpubReaderHighlightsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int ENTER_ACTIONS_MODE_MS = 700;
}  // namespace

EpubReaderHighlightsActivity::EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                           const std::shared_ptr<Epub>& epub,
                                                           const std::string& epubPath)
    : UiListActivity("EpubReaderHighlights", renderer, mappedInput, /*wantsTouchLongPress=*/true),
      epub(epub),
      epubPath(epubPath) {}

void EpubReaderHighlightsActivity::onEnter() {
  UiListActivity::onEnter();
  if (!epub) return;

  if (!HighlightFile::load(epubPath, highlights)) {
    highlights.shrink_to_fit();
  }
  // The file keeps creation order; the list reads in book order.
  std::sort(highlights.begin(), highlights.end(), [](const HighlightEntry& a, const HighlightEntry& b) {
    return a.spineIndex != b.spineIndex ? a.spineIndex < b.spineIndex : a.start < b.start;
  });
  LOG_DBG("EPH", "Loaded %d highlights for book: %s", static_cast<int>(highlights.size()), epubPath.c_str());
  rebuildHighlightRowItems();
}

void EpubReaderHighlightsActivity::rebuildHighlightRowItems() {
  highlightSubtitles.clear();
  highlightRowItems.clear();
  if (!epub) return;
  highlightSubtitles.reserve(highlights.size());
  highlightRowItems.reserve(highlights.size());
  for (const auto& highlight : highlights) {
    const int tocIndex = epub->getTocIndexForSpineIndex(highlight.spineIndex);
    highlightSubtitles.push_back(tocIndex >= 0 ? epub->getTocItem(tocIndex).title : std::string(tr(STR_UNNAMED)));

    fui::ListItem item;
    item.label = highlight.text.c_str();
    item.subtitle = highlightSubtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(highlightRowItems.size());
    highlightRowItems.push_back(item);
  }
}

void EpubReaderHighlightsActivity::openSelectedHighlight() {
  if (highlights.empty() || nav.selected < 0 || nav.selected >= listCount()) return;
  const auto& highlight = highlights[nav.selected];
  ProgressChangeResult result{};
  result.spineIndex = highlight.spineIndex;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = highlight.start;
  setResult(std::move(result));
  finish();
}

void EpubReaderHighlightsActivity::activateIndex(const int index) {
  if (confirmPopup.isActive()) return;
  // A row index captured before a delete shrank the list.
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelectedHighlight();
}

void EpubReaderHighlightsActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive()) return;
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showHighlightActions();
}

bool EpubReaderHighlightsActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    // Popup dismissed without a selection: cancel the delete.
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool EpubReaderHighlightsActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, ENTER_ACTIONS_MODE_MS)) {
    showHighlightActions();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelectedHighlight();
    return true;
  }
  return false;
}

void EpubReaderHighlightsActivity::showHighlightActions() {
  if (highlights.empty() || confirmPopup.isActive()) return;
  const StrId options[] = {StrId::STR_OPEN, StrId::STR_DELETE};
  confirmPopup.show(StrId::STR_HIGHLIGHTS, options, 2, 0, [this](const int idx) {
    if (idx == 0) {
      openSelectedHighlight();
    } else if (idx == 1) {
      showDeleteConfirmation();
    }
  });
  requestUpdate();
}

void EpubReaderHighlightsActivity::showDeleteConfirmation() {
  if (highlights.empty() || confirmPopup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_CONFIRM_DELETE_HIGHLIGHT), options, 2, 0, [this](int idx) {
    confirmingDelete = false;
    if (idx == 1) deleteSelectedHighlight();
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderHighlightsActivity::deleteSelectedHighlight() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  highlights.erase(highlights.begin() + nav.selected);
  // Re-derive the rows before the SD save so the render task never sees rows
  // aliasing the erased storage.
  rebuildHighlightRowItems();
  if (!HighlightFile::save(epubPath, highlights)) {
    LOG_ERR("EPH", "Failed to save highlights after delete");
  }
  if (nav.selected >= listCount() && nav.selected > 0) nav.selected--;
  nav.follow(listCount());
  requestUpdate(true);
}

void EpubReaderHighlightsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the title band render() paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (highlights.empty()) {
    screen.centeredText(tr(STR_NO_HIGHLIGHTS), screen.theme().bodyText);
    return;
  }

  // This hint names a physical button, so omit it on touch boards.
  if (!mappedInput.hasTouch()) {
    const int helpLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpLineHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpLineHeight},
                     tr(STR_HOLD_OPEN_FOR_ACTIONS));
  }

  fui::ListProps props;
  props.items = highlightRowItems.data();
  props.count = static_cast<uint16_t>(highlightRowItems.size());
  props.action = ACTION_ROW;
  // Tap opens; long-press shows the actions (physical buttons stay in loop()).
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderHighlightsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto orientation = renderer.getOrientation();
  // Same hint gutters as the bookmarks list.
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isPortraitInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? 40 : 0;
  const int contentX = isLandscapeCw ? hintGutterWidth : 0;
  const int contentWidth = pageWidth - hintGutterWidth;
  const int contentY = isPortraitInverted ? 50 : 0;

  const int titleX =
      contentX + (contentWidth - renderer.getTextWidth(UI_12_FONT_ID, tr(STR_HIGHLIGHTS), EpdFontFamily::BOLD)) / 2;
  renderer.drawText(UI_12_FONT_ID, titleX, 15 + contentY, tr(STR_HIGHLIGHTS), true, EpdFontFamily::BOLD);

  renderUi();

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  const auto confirmLabel = highlights.empty() ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
