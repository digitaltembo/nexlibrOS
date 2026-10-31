#include "EpubReaderClippingListActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <tuple>

#include "ClippingStore.h"
#include "DictionaryDefinitionActivity.h"
#include "MappedInputManager.h"
#include "clippings/ClippingPreview.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {

constexpr int ENTER_ACTIONS_MODE_MS = 700;

}  // namespace

EpubReaderClippingListActivity::EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("EpubReaderClippings", renderer, mappedInput, /*wantsTouchLongPress=*/true) {}

void EpubReaderClippingListActivity::onEnter() {
  UiListActivity::onEnter();
  initialListRender = true;
  rebuildRows();
}

int EpubReaderClippingListActivity::listCount() const { return static_cast<int>(CLIPPINGS.clippingCount()); }

const char* EpubReaderClippingListActivity::headerTitle() const { return tr(STR_CLIPPINGS); }

void EpubReaderClippingListActivity::rebuildRows() {
  windowStart = -1;
  windowCount = 0;
  // 2 bytes per highlight. Without it the rows simply stay in creation order.
  const size_t count = CLIPPINGS.clippingCount();
  order = count ? makeUniqueNoThrow<uint16_t[]>(count) : nullptr;
  if (!order) return;
  for (size_t i = 0; i < count; ++i) order[i] = static_cast<uint16_t>(i);
  const auto key = [](const uint16_t index) {
    const Clipping& clipping = *CLIPPINGS.clippingAt(index);
    const bool anchored = clipping.startOffset != UINT32_MAX;
    return std::make_tuple(clipping.spineIndex, !anchored,
                           anchored ? clipping.startOffset : static_cast<uint32_t>(clipping.startPage), index);
  };
  std::sort(order.get(), order.get() + count, [&](const uint16_t a, const uint16_t b) { return key(a) < key(b); });
}

size_t EpubReaderClippingListActivity::storeIndex(const int row) const {
  return order ? order[row] : static_cast<size_t>(row);
}

void EpubReaderClippingListActivity::refreshRowWindow(const int start) {
  const int total = listCount();
  int clamped = std::max(0, start);
  if (clamped > total - ROW_WINDOW) clamped = std::max(0, total - ROW_WINDOW);
  if (clamped == windowStart) return;

  windowCount = std::min(ROW_WINDOW, total - clamped);
  for (int slot = 0; slot < windowCount; ++slot) {
    const size_t clippingIndex = storeIndex(clamped + slot);
    const Clipping* clipping = CLIPPINGS.clippingAt(clippingIndex);
    previewLengths[slot] = 0;
    previews[slot][0] = '\0';
    if (!clipping || !CLIPPINGS.readClippingPreview(clippingIndex, previews[slot].data(), previews[slot].size(),
                                                    previewLengths[slot])) {
      LOG_ERR("CLIP", "Failed to read clipping %u", static_cast<unsigned>(clippingIndex));
    }

    fui::ListItem item;
    item.label = previews[slot].data();
    item.subtitle = clipping && clipping->chapterTitle[0] != '\0' ? clipping->chapterTitle : tr(STR_UNNAMED);
    item.actionValue = static_cast<int16_t>(clamped + slot);
    rowItems[slot] = item;
  }
  windowStart = clamped;
}

void EpubReaderClippingListActivity::openSelected() {
  if (confirmPopup.isActive() || nav.selected < 0 || nav.selected >= listCount()) return;
  const Clipping* clipping = CLIPPINGS.clippingAt(storeIndex(nav.selected));
  if (!clipping) return;
  // Without source offsets there is no reliable place to jump to.
  if (clipping->startOffset == UINT32_MAX || clipping->endOffset == UINT32_MAX) {
    readSelected();
    return;
  }
  ProgressChangeResult result;
  result.spineIndex = clipping->spineIndex;
  result.page = clipping->startPage;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = clipping->startOffset;
  setResult(std::move(result));
  finish();
}

void EpubReaderClippingListActivity::showActions() {
  if (listCount() == 0 || confirmPopup.isActive()) return;
  static constexpr StrId OPTIONS[] = {StrId::STR_OPEN, StrId::STR_DELETE};
  confirmPopup.show(StrId::STR_CLIPPINGS, OPTIONS, 2, 0, [this](const int index) {
    if (index == 0) {
      readSelected();
    } else if (index == 1) {
      showDeleteConfirmation();
    }
  });
  requestUpdate();
}

void EpubReaderClippingListActivity::readSelected() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  const size_t index = storeIndex(nav.selected);
  const Clipping* clipping = CLIPPINGS.clippingAt(index);
  std::string text;
  if (!clipping || !CLIPPINGS.readClippingText(index, text)) return;
  const std::string title = clipping->chapterTitle[0] != '\0' ? clipping->chapterTitle : tr(STR_CLIPPINGS);
  auto detail = makeUniqueNoThrow<DictionaryDefinitionActivity>(renderer, mappedInput, title, std::move(text));
  if (!detail) {
    LOG_ERR("CLIP", "Failed to allocate clipping detail activity");
    return;
  }
  startActivityForResult(std::move(detail), [this](const ActivityResult&) { requestUpdate(); });
}

void EpubReaderClippingListActivity::activateIndex(const int index) {
  if (confirmPopup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  openSelected();
}

void EpubReaderClippingListActivity::onRowLongPress(const int index) {
  if (confirmPopup.isActive() || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showActions();
}

void EpubReaderClippingListActivity::showDeleteConfirmation() {
  if (listCount() == 0 || confirmPopup.isActive()) return;
  confirmingDelete = true;
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  confirmPopup.show(tr(STR_DELETE_CLIPPING_CONFIRM), options, 2, 0, [this](const int index) {
    confirmingDelete = false;
    if (index == 1) deleteSelected();
    requestUpdate();
  });
  requestUpdate();
}

void EpubReaderClippingListActivity::deleteSelected() {
  if (nav.selected < 0 || nav.selected >= listCount()) return;
  {
    RenderLock lock;  // the render task reads the store and the row order
    if (!CLIPPINGS.removeClippingAt(storeIndex(nav.selected))) {
      LOG_ERR("CLIP", "Failed to delete clipping %d", static_cast<int>(nav.selected));
      return;
    }
    rebuildRows();
  }
  if (listCount() == 0) {
    finish();
    return;
  }
  if (nav.selected >= listCount()) nav.selected = listCount() - 1;
  nav.follow(listCount());
}

bool EpubReaderClippingListActivity::handleCustomInput() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (confirmingDelete) {
    confirmingDelete = false;
    requestUpdate();
    return true;
  }
  return false;
}

bool EpubReaderClippingListActivity::handleButtons() {
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, ENTER_ACTIONS_MODE_MS)) {
    showActions();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return true;
  }
  return false;
}

void EpubReaderClippingListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (listCount() == 0) {
    screen.centeredText(tr(STR_NO_CLIPPINGS), screen.theme().bodyText);
    return;
  }

  if (!mappedInput.hasTouch()) {
    const int helpHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const fui::Rect band = screen.takeBottom(static_cast<int16_t>(helpHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{band.x, band.y + metrics.verticalSpacing, band.width, helpHeight},
                     tr(STR_HOLD_OPEN_FOR_ACTIONS));
  }

  fui::ListProps props;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  syncListViewport(screen, props);
  refreshRowWindow(nav.top);
  props.items = rowItems.data();
  props.itemsWindowFirst = static_cast<uint16_t>(windowStart);
  props.itemsWindowCount = static_cast<uint16_t>(windowCount);
  screen.list(props);
}

void EpubReaderClippingListActivity::render(RenderLock&& lock) {
  if (initialListRender && listCount() > 0) {
    renderer.clearScreen();
    GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  }
  initialListRender = false;
  UiListActivity::render(std::move(lock));
  if (confirmPopup.processRender(renderer, mappedInput)) return;
}
