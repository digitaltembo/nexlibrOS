#include "EpubReaderSearchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Text scanned per loop pass: small enough that Back stays responsive, large
// enough that the per-pass overhead does not dominate.
constexpr size_t SEARCH_BYTES_PER_PASS = 16 * 1024;
// Progress repaints are e-ink refreshes; only redraw every few percent.
constexpr int PROGRESS_REDRAW_STEP = 10;
}  // namespace

EpubReaderSearchActivity::EpubReaderSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const std::shared_ptr<Epub>& epub, std::string term,
                                                   const BookPageEstimate& pages, const int currentSpine,
                                                   const int currentChapterPages)
    : UiListActivity("EpubReaderSearch", renderer, mappedInput),
      epub(epub),
      term(std::move(term)),
      pages(pages),
      currentSpine(currentSpine),
      currentChapterPages(currentChapterPages) {}

void EpubReaderSearchActivity::onEnter() {
  UiListActivity::onEnter();
  if (!epub) {
    state = State::Failed;
    return;
  }
  BookTextIndex::Reader existing;
  if (existing.open(*epub)) {
    existing.file().close();
    startSearch();
    return;
  }
  builder = makeUniqueNoThrow<BookTextIndex::Builder>();
  if (!builder || !builder->begin(epub)) {
    LOG_ERR("ERSR", "Could not start the text index");
    builder.reset();
    state = State::Failed;
    return;
  }
  state = State::Indexing;
  requestUpdate();
}

void EpubReaderSearchActivity::setProgress(const int percent) {
  if (percent / PROGRESS_REDRAW_STEP == progress / PROGRESS_REDRAW_STEP) {
    progress = percent;
    return;
  }
  progress = percent;
  requestUpdate();
}

void EpubReaderSearchActivity::startSearch() {
  progress = 0;
  if (!search.begin(*epub, term)) {
    state = State::Failed;
  } else {
    state = State::Searching;
  }
  requestUpdate();
}

void EpubReaderSearchActivity::loop() {
  if (state == State::Indexing) {
    bool added;
    {
      // Inflate's 32 KB window comes out of the lent framebuffer (via
      // buildscratch) instead of a contiguous heap block, as chapter builds
      // do. Nothing may draw during the loan, hence the lock; the next render
      // repaints the whole screen over the white buffer the loan returns.
      RenderLock lock;
      GfxRenderer::FrameBufferLoan loan(renderer);
      added = builder->addSpine(nextSpine);
    }
    if (!added) {
      builder.reset();
      state = State::Failed;
      requestUpdate();
    } else if (++nextSpine >= builder->spineCount()) {
      const bool built = builder->finish();
      builder.reset();
      if (built) {
        startSearch();
      } else {
        state = State::Failed;
        requestUpdate();
      }
    } else {
      setProgress(nextSpine * 100 / builder->spineCount());
    }
  } else if (state == State::Searching) {
    if (search.step(SEARCH_BYTES_PER_PASS)) {
      setProgress(search.progressPercent());
    } else {
      finishSearch();
    }
  }
  UiListActivity::loop();
}

void EpubReaderSearchActivity::finishSearch() {
  RenderLock lock;  // the render task reads the rows
  auto& matches = search.matches();
  rowSubtitles.clear();
  rowItems.clear();
  rowSubtitles.reserve(matches.size());
  rowItems.reserve(matches.size());
  for (const auto& match : matches) {
    char position[32];
    const int page = pages.pageAt(*epub, match.spine, match.chapterFraction, currentSpine, currentChapterPages);
    if (page > 0) {
      snprintf(position, sizeof(position), tr(STR_PAGE_NUMBER_FORMAT), page);
    } else {
      const float progressFraction = epub->calculateProgress(match.spine, match.chapterFraction);
      snprintf(position, sizeof(position), "%d%%", static_cast<int>(progressFraction * 100.0f + 0.5f));
    }
    std::string subtitle = position;
    const int tocIndex = epub->getTocIndexForSpineIndex(match.spine);
    if (tocIndex >= 0) subtitle += " \xC2\xB7 " + epub->getTocItem(tocIndex).title;
    rowSubtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = match.snippet.c_str();
    item.subtitle = rowSubtitles.back().c_str();
    item.actionValue = static_cast<int16_t>(rowItems.size());
    rowItems.push_back(item);
  }
  char heading[48];
  snprintf(heading, sizeof(heading),
           search.truncated() ? tr(STR_MATCH_COUNT_CAPPED_FORMAT) : tr(STR_MATCH_COUNT_FORMAT),
           static_cast<int>(matches.size()));
  title = heading;
  state = State::Done;
  nav.reset();
  requestUpdate();
}

void EpubReaderSearchActivity::activateIndex(const int index) {
  if (state != State::Done || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  const auto& match = search.matches()[index];
  ProgressChangeResult result{};
  result.spineIndex = match.spine;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = match.offset;
  setResult(std::move(result));
  finish();
}

bool EpubReaderSearchActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return true;
  }
  if (state == State::Done && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void EpubReaderSearchActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the title band render() paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  char status[48];
  switch (state) {
    case State::Indexing:
      snprintf(status, sizeof(status), "%s... %d%%", tr(STR_INDEXING), progress);
      screen.centeredText(status, screen.theme().bodyText);
      return;
    case State::Searching:
      snprintf(status, sizeof(status), "%s... %d%%", tr(STR_SEARCHING), progress);
      screen.centeredText(status, screen.theme().bodyText);
      return;
    case State::Failed:
      screen.centeredText(tr(STR_SEARCH_FAILED), screen.theme().bodyText);
      return;
    case State::Done:
      break;
  }
  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_MATCHES), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  // Two lines of context per match.
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

const char* EpubReaderSearchActivity::headerTitle() const {
  return state == State::Done ? title.c_str() : tr(STR_SEARCH);
}

void EpubReaderSearchActivity::drawFooter() {
  const auto confirmLabel = rowItems.empty() ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
