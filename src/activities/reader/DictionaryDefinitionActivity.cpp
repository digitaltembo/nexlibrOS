#include "DictionaryDefinitionActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DictHtmlPages.h"
#include "util/HtmlToPlainText.h"

namespace {

// Longest measurable/drawable span. Wrapped lines stay under the screen width
// (far below this); only pathological unbreakable tokens are split at this cap.
constexpr size_t MAX_LINE_BYTES = 191;

// Body text left/right inset, matching the reader's default feel.
constexpr int SIDE_PADDING = 20;

// Styled-path ceiling: the laid-out Pages keep the whole definition resident
// (TextBlock arenas ≈ text + ~7 bytes/word plus per-line objects), roughly
// doubling the string's footprint while this activity is stacked over the
// reader and word-select. Bigger definitions take the span-based plain-text
// path, which holds no per-page copies.
constexpr size_t MAX_STYLED_HTML_BYTES = 16 * 1024;

// Dictionary tabs: one equal-width slot per dictionary, the active one a
// filled pill.
constexpr int TAB_FONT_ID = UI_10_FONT_ID;
constexpr int TAB_PILL_PAD_X = 8;
constexpr int TAB_PILL_INSET_Y = 6;
constexpr int TAB_PILL_RADIUS = 6;
constexpr int TAB_BODY_GAP = 10;  // tab divider to the first line of the definition
constexpr const char* TAB_SEARCHING_LABEL = "...";

// Free heap a tab switch keeps beyond its definition copy: std::string growth
// aborts on OOM under -fno-exceptions.
constexpr size_t TAB_COPY_HEADROOM_BYTES = 8 * 1024;

// The background task only runs lookups (no layout or rendering), the same
// call depth the 8KB loop task needs for them. Below the loop and render
// tasks' priority, so it works only while the shown definition and input
// handling are idle.
constexpr uint32_t BG_TASK_STACK_BYTES = 8 * 1024;
constexpr UBaseType_t BG_TASK_PRIORITY = 0;  // tskIDLE_PRIORITY, which the simulator shim lacks

}  // namespace

void DictionaryDefinitionActivity::onEnter() {
  Activity::onEnter();
  if (dictLookup && !dictionaries.empty()) {
    tabs.reserve(dictionaries.size());  // never reallocates while tabs arrive
    DictResult first;
    first.name = std::move(dictionaries[0].name);
    first.headword = headword;
    first.found = true;
    first.html = htmlDefinition;
    // layoutDefinition() consumes `definition`; a copy lets this tab come
    // back without a lookup.
    size_t budget = RESULT_CACHE_BYTES;
    if (tabBand && definition.size() <= budget &&
        ESP.getMaxAllocHeap() >= definition.size() + TAB_COPY_HEADROOM_BYTES) {
      first.definition = definition;
      first.retained = true;
      budget -= definition.size();
    }
    tabs.push_back(std::move(first));

    bgSlots.reserve(dictionaries.size() - 1);
    for (size_t i = 1; i < dictionaries.size(); i++) {
      bgSlots.emplace_back();
      bgSlots.back().name = std::move(dictionaries[i].name);
    }
    bgCacheBudget = budget;
    std::vector<DictionaryEntry>().swap(dictionaries);
  }
  layoutDefinition();
  startBackgroundLookups();
  requestUpdate();
}

void DictionaryDefinitionActivity::startBackgroundLookups() {
  if (bgSlots.empty()) return;
  // Its own Dictionary: the caller's stays open on the word-select side, and
  // keeping the task's state separate means no handle is shared across tasks.
  bgLookup = makeUniqueNoThrow<DictionaryLookup>();
  bgDone = xSemaphoreCreateBinary();
  if (bgLookup) bgLookup->setCancelFlag(&bgCancel);
  if (!bgLookup || !bgDone ||
      xTaskCreate(&DictionaryDefinitionActivity::backgroundTrampoline, "DictLookup", BG_TASK_STACK_BYTES, this,
                  BG_TASK_PRIORITY, &bgTask) != pdPASS) {
    LOG_ERR("DDA", "OOM: background lookup task (max block %u)", static_cast<unsigned>(ESP.getMaxAllocHeap()));
    bgTask = nullptr;
    stopBackgroundLookups();
    bgSlots.clear();  // no "..." for lookups that will never run
  }
}

void DictionaryDefinitionActivity::stopBackgroundLookups() {
  if (bgTask) {
    bgCancel.store(true);
    // No timeout: deleting the task mid-lookup could leave storageMutex held.
    // The wait is bounded — the task checks bgCancel between dictionaries and
    // every 64KB of an index build.
    xSemaphoreTake(bgDone, portMAX_DELAY);
    bgTask = nullptr;
  }
  if (bgDone) {
    vSemaphoreDelete(bgDone);
    bgDone = nullptr;
  }
  bgLookup.reset();
}

void DictionaryDefinitionActivity::backgroundTrampoline(void* param) {
  static_cast<DictionaryDefinitionActivity*>(param)->backgroundMain();
}

void DictionaryDefinitionActivity::backgroundMain() {
  // Read once: after the last publish loop() may free bgSlots.
  const size_t count = bgSlots.size();
  DictResult* const slots = bgSlots.data();
  for (size_t i = 0; i < count && !bgCancel.load(); i++) {
    DictResult& slot = slots[i];
    bgLookup->prepare(slot.name.c_str());
    StrId error = StrId::STR_DICT_NOT_FOUND;
    slot.found = bgLookup->lookup(lookupWord.c_str(), slot.definition, slot.headword, error);
    if (slot.found) {
      slot.html = bgLookup->definitionsAreHtml();
      slot.retained = slot.definition.size() <= bgCacheBudget;
      if (slot.retained) bgCacheBudget -= slot.definition.size();
    } else {
      LOG_DBG("DDA", "'%s' not in %s (%s)", lookupWord.c_str(), slot.name.c_str(), I18N.get(error));
    }
    if (!slot.retained) {
      std::string().swap(slot.definition);
    }
    bgPublished.store(i + 1);
  }
  xSemaphoreGive(bgDone);
  vTaskDelete(nullptr);
}

bool DictionaryDefinitionActivity::collectBackgroundResults() {
  if (!searching()) return false;
  const size_t published = bgPublished.load();
  if (published == bgCollected) return false;

  {
    RenderLock lock;
    for (; bgCollected < published; bgCollected++) {
      DictResult& slot = bgSlots[bgCollected];
      if (slot.found) tabs.push_back(std::move(slot));
    }
    if (!searching()) std::vector<DictResult>().swap(bgSlots);
  }
  if (bgSlots.empty()) {
    // Every slot published: the task has given bgDone (or is about to) and
    // needs nothing more from this activity.
    stopBackgroundLookups();
  }
  return true;
}

void DictionaryDefinitionActivity::layoutDefinition() {
  // Normalize StarDict multi-type separators so the wrap loop and the
  // C-string font APIs below both see the whole definition.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  if (!(htmlDefinition && definition.size() <= MAX_STYLED_HTML_BYTES && layoutHtmlPages())) {
    definition = htmlToPlainText(definition);
    wrapText();
  }
}

void DictionaryDefinitionActivity::clearContent() {
  pages.clear();
  std::vector<Line>().swap(lines);
  definition.clear();
  definition.shrink_to_fit();
  currentPage = 0;
  totalPages = 1;
}

void DictionaryDefinitionActivity::switchTab(const int index) {
  if (index == activeTab || index < 0 || index >= static_cast<int>(tabs.size())) return;
  showTab(index);
}

void DictionaryDefinitionActivity::showTab(const int index) {
  DictResult& tab = tabs[index];
  {
    RenderLock lock;
    activeTab = index;
    // Drop the old definition before taking the new one so both are never
    // resident at once.
    clearContent();
    if (tab.retained && ESP.getMaxAllocHeap() >= tab.definition.size() + TAB_COPY_HEADROOM_BYTES) {
      busy = false;
      hasMessage = false;
      deferredTab = -1;
      headword = tab.headword;
      definition = tab.definition;
      htmlDefinition = tab.html;
      layoutDefinition();
    } else {
      // Re-read from SD. While the task runs that waits for it to finish.
      busy = true;
      busyMessage = StrId::STR_DICT_LOOKING_UP;
      headword = tab.headword;
      deferredTab = bgTask ? index : -1;
    }
  }
  if (!busy) {
    requestUpdate();
    return;
  }
  if (deferredTab >= 0) {
    requestUpdate();  // loop() resumes this once the background task is done
    return;
  }

  if (dictLookup->prepare(tab.name.c_str())) busyMessage = StrId::STR_DICT_INDEXING;
  requestUpdateAndWait();  // paint the new tab + busy popup before blocking on SD

  std::string newDefinition;
  std::string newHeadword;
  StrId error = StrId::STR_DICT_NOT_FOUND;
  const bool found = dictLookup->lookup(lookupWord.c_str(), newDefinition, newHeadword, error);
  {
    RenderLock lock;
    busy = false;
    hasMessage = !found;
    if (found) {
      headword = std::move(newHeadword);
      definition = std::move(newDefinition);
      htmlDefinition = dictLookup->definitionsAreHtml();
      layoutDefinition();
    } else {
      message = error;
    }
  }
  requestUpdate();
}

const char* DictionaryDefinitionActivity::slotLabel(const int index) const {
  return index < static_cast<int>(tabs.size()) ? tabs[index].name.c_str() : TAB_SEARCHING_LABEL;
}

Rect DictionaryDefinitionActivity::contentRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto orientation = renderer.getOrientation();
  const bool isLandscapeCw = orientation == GfxRenderer::Orientation::LandscapeClockwise;
  const bool isLandscapeCcw = orientation == GfxRenderer::Orientation::LandscapeCounterClockwise;
  const bool isInverted = orientation == GfxRenderer::Orientation::PortraitInverted;
  const int hintGutterWidth = (isLandscapeCw || isLandscapeCcw) ? metrics.sideButtonHintsWidth : 0;
  const int contentY = isInverted ? metrics.buttonHintsHeight : 0;
  return Rect{isLandscapeCw ? hintGutterWidth : 0, contentY, renderer.getScreenWidth() - hintGutterWidth,
              renderer.getScreenHeight() - contentY};
}

Rect DictionaryDefinitionActivity::tabBarRect() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = contentRect();
  return Rect{content.x, content.y + metrics.topPadding + metrics.headerHeight, content.width,
              showTabs() ? metrics.tabBarHeight : 0};
}

// Horizontal span of tab `index`. When every full name fits, each slot hugs
// its label and the spare width is shared out; otherwise the slots are equal
// and drawTabs() truncates the labels to fit.
void DictionaryDefinitionActivity::tabSlot(const Rect& bar, const int index, int& x, int& width) const {
  const int count = slotCount();
  const int innerX = bar.x + SIDE_PADDING;
  const int innerWidth = bar.width - 2 * SIDE_PADDING;
  const auto naturalWidth = [this](const int i) {
    return renderer.getTextWidth(TAB_FONT_ID, slotLabel(i)) + 2 * TAB_PILL_PAD_X;
  };

  int total = 0;
  for (int i = 0; i < count; i++) total += naturalWidth(i);
  if (total > innerWidth) {
    width = innerWidth / count;
    x = innerX + index * width;
    return;
  }
  const int extra = (innerWidth - total) / count;
  x = innerX;
  for (int i = 0; i < index; i++) x += naturalWidth(i) + extra;
  width = naturalWidth(index) + extra;
}

int DictionaryDefinitionActivity::tabAt(const int x, const int y) const {
  if (!showTabs()) return -1;
  const Rect bar = tabBarRect();
  if (y < bar.y || y >= bar.y + bar.height) return -1;
  for (int i = 0; i < static_cast<int>(tabs.size()); i++) {
    int slotX = 0;
    int slotWidth = 0;
    tabSlot(bar, i, slotX, slotWidth);
    if (x >= slotX && x < slotX + slotWidth) return i;
  }
  return -1;
}

int DictionaryDefinitionActivity::bodyTop() const {
  const Rect bar = tabBarRect();
  return bar.y + bar.height + (showTabs() ? TAB_BODY_GAP : 0);
}

void DictionaryDefinitionActivity::drawTabs(const Rect rect) const {
  const int lineHeight = renderer.getLineHeight(TAB_FONT_ID);
  const int pillY = rect.y + TAB_PILL_INSET_Y;
  const int pillHeight = rect.height - 2 * TAB_PILL_INSET_Y;

  const int count = slotCount();
  for (int i = 0; i < count; i++) {
    int slotX = 0;
    int slotWidth = 0;
    tabSlot(rect, i, slotX, slotWidth);
    const std::string label = renderer.truncatedText(TAB_FONT_ID, slotLabel(i), slotWidth - 2 * TAB_PILL_PAD_X);
    const int pillWidth = renderer.getTextWidth(TAB_FONT_ID, label.c_str()) + 2 * TAB_PILL_PAD_X;
    const int pillX = slotX + (slotWidth - pillWidth) / 2;
    const bool selected = i == activeTab;
    if (selected) {
      renderer.fillRoundedRect(pillX, pillY, pillWidth, pillHeight, TAB_PILL_RADIUS, Color::Black);
    }
    renderer.drawText(TAB_FONT_ID, pillX + TAB_PILL_PAD_X, pillY + (pillHeight - lineHeight) / 2, label.c_str(),
                      !selected);
  }
  const int dividerY = rect.y + rect.height - 1;
  renderer.drawLine(rect.x + SIDE_PADDING, dividerY, rect.x + rect.width - SIDE_PADDING - 1, dividerY);
}

void DictionaryDefinitionActivity::onExit() {
  stopBackgroundLookups();
  Activity::onExit();
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
  }
}

DictionaryDefinitionActivity::BodyArea DictionaryDefinitionActivity::bodyArea() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int bottomArea = metrics.buttonHintsHeight + metrics.verticalSpacing;
  return {contentRect().width - 2 * SIDE_PADDING, renderer.getScreenHeight() - bodyTop() - bottomArea};
}

// Styled path: lay the HTML definition out through the EPUB chapter parser
// into reader-identical Pages. Frees `definition` on success (the page arenas
// own the text); any failure leaves state untouched for the plain-text path.
bool DictionaryDefinitionActivity::layoutHtmlPages() {
  const BodyArea body = bodyArea();
  if (body.width <= 0 || body.height <= 0) return false;
  if (!buildDictionaryHtmlPages(renderer, definition, static_cast<uint16_t>(body.width),
                                static_cast<uint16_t>(body.height), pages)) {
    return false;
  }
  definition.clear();
  definition.shrink_to_fit();
  totalPages = static_cast<int>(pages.size());
  currentPage = 0;
  return true;
}

int DictionaryDefinitionActivity::measureSpan(const int fontId, const char* text, size_t len) const {
  char buf[MAX_LINE_BYTES + 1];
  len = std::min(len, MAX_LINE_BYTES);
  memcpy(buf, text, len);
  buf[len] = '\0';
  return renderer.getTextAdvanceX(fontId, buf, EpdFontFamily::REGULAR);
}

// Greedy word-wrap of `definition` into byte spans. '\n' breaks lines (blank
// lines survive as paragraph spacing; NULs from multi-type StarDict entries
// were normalized to newlines in onEnter); '\r' is dropped by treating it as
// a space at a token edge.
void DictionaryDefinitionActivity::wrapText() {
  lines.clear();
  lines.reserve(definition.size() / 32 + 8);

  const int fontId = SETTINGS.getReaderFontId();
  // SD-card fonts: merge every definition codepoint into the persistent
  // advance table up front. Otherwise each unseen codepoint measured below
  // falls back to an on-demand glyph load from SD (8-slot overflow ring).
  renderer.ensureSdCardFontReady(fontId, definition.c_str(), 0x01 /* REGULAR */);

  const BodyArea body = bodyArea();
  const int maxWidth = body.width;
  const int spaceWidth = renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR);
  const int lineHeight = renderer.getLineHeight(fontId);
  linesPerPage = std::max(1, body.height / lineHeight);

  const char* text = definition.c_str();
  const uint32_t n = static_cast<uint32_t>(definition.size());
  uint32_t lineStart = 0;
  uint32_t lineEnd = 0;  // one past the last token byte on the current line
  int lineWidth = 0;

  const auto flushLine = [&](uint32_t nextStart) {
    lines.push_back({lineStart, static_cast<uint16_t>(lineEnd - lineStart)});
    lineStart = nextStart;
    lineEnd = nextStart;
    lineWidth = 0;
  };

  uint32_t i = 0;
  while (i < n) {
    const char c = text[i];
    if (c == '\n' || c == '\0') {
      flushLine(i + 1);
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }

    // Token: run of non-whitespace bytes, capped at the measure buffer.
    const uint32_t tokenStart = i;
    while (i < n && text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n' && text[i] != '\0' &&
           i - tokenStart < MAX_LINE_BYTES) {
      i++;
    }
    // If the byte cap cut the token mid-UTF-8-sequence, back off to the last
    // complete codepoint so measure/draw never see a partial sequence. A
    // natural stop lands on whitespace or the terminating NUL, never on a
    // continuation byte, so this is a no-op there.
    while (i - tokenStart > 1 && (text[i] & 0xC0) == 0x80) i--;
    const uint32_t tokenLen = i - tokenStart;
    const int tokenWidth = measureSpan(fontId, text + tokenStart, tokenLen);

    if (lineEnd == lineStart) {
      lineStart = tokenStart;
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    } else if (lineWidth + spaceWidth + tokenWidth <= maxWidth &&
               tokenStart + tokenLen - lineStart <= UINT16_MAX) {  // span len must fit Line::len
      lineEnd = tokenStart + tokenLen;
      lineWidth += spaceWidth + tokenWidth;
    } else {
      flushLine(tokenStart);
      lineEnd = tokenStart + tokenLen;
      lineWidth = tokenWidth;
    }

    // An unbreakable token wider than the screen is now alone on the line
    // (any previous content was flushed above): split it at the widest
    // fitting UTF-8 boundary and carry the remainder forward.
    while (lineWidth > maxWidth && lineEnd - lineStart > 1) {
      const uint32_t len = lineEnd - lineStart;
      uint32_t lastFit = 0;
      for (uint32_t f = 1; f <= len; f++) {
        if (f == len || (text[lineStart + f] & 0xC0) != 0x80) {  // codepoint boundary
          if (measureSpan(fontId, text + lineStart, f) > maxWidth) break;
          lastFit = f;
        }
      }
      if (lastFit == 0) {
        // Even a single over-wide glyph must make progress; consume its whole
        // UTF-8 sequence rather than splitting it into invalid fragments.
        lastFit = 1;
        while (lastFit < len && (text[lineStart + lastFit] & 0xC0) == 0x80) lastFit++;
      }
      const uint32_t rest = lineStart + lastFit;
      lineEnd = rest;
      flushLine(rest);
      lineEnd = rest + (len - lastFit);
      lineWidth = measureSpan(fontId, text + lineStart, lineEnd - lineStart);
    }
  }
  if (lineEnd > lineStart) flushLine(n);

  // Trim trailing blank lines so the last page is not empty padding.
  while (!lines.empty() && lines.back().len == 0) lines.pop_back();

  totalPages = std::max(1, (static_cast<int>(lines.size()) + linesPerPage - 1) / linesPerPage);
  currentPage = 0;
}

void DictionaryDefinitionActivity::loop() {
  if (collectBackgroundResults()) requestUpdate();
  if (deferredTab >= 0 && !bgTask) {
    const int index = deferredTab;
    deferredTab = -1;
    showTab(index);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (tabs.size() > 1 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switchTab((activeTab + 1) % static_cast<int>(tabs.size()));
    return;
  }

  // Same tap zones as the reader page turns: left third = previous page,
  // the rest = next. Back is the header button or the usual left-edge swipe.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenTapped(tx, ty)) {
    const int tab = tabAt(tx, ty);
    if (tab >= 0) {
      switchTab(tab);
    } else if (tx < renderer.getScreenWidth() / 3) {
      if (currentPage > 0) {
        currentPage--;
        requestUpdate();
      }
    } else if (currentPage + 1 < totalPages) {
      currentPage++;
      requestUpdate();
    }
    return;
  }

  buttonNavigator.onNext([this] {
    if (currentPage + 1 < totalPages) {
      currentPage++;
      requestUpdate();
    }
  });

  buttonNavigator.onPrevious([this] {
    if (currentPage > 0) {
      currentPage--;
      requestUpdate();
    }
  });
}

// Draws the current page: a styled Page when the HTML layout succeeded,
// otherwise the wrapped line spans (copied into a stack buffer for NUL
// termination). Called twice per render: once in font-cache scan mode, once
// for the real paint.
void DictionaryDefinitionActivity::drawBody(const int fontId, const int x, const int startY) const {
  if (!pages.empty()) {
    pages[currentPage]->render(renderer, fontId, x, startY);
    return;
  }
  const int lineHeight = renderer.getLineHeight(fontId);
  char buf[MAX_LINE_BYTES + 1];
  const int firstLine = currentPage * linesPerPage;
  const int lastLine = std::min(firstLine + linesPerPage, static_cast<int>(lines.size()));
  for (int i = firstLine; i < lastLine; i++) {
    if (lines[i].len == 0) continue;
    const size_t len = std::min(static_cast<size_t>(lines[i].len), MAX_LINE_BYTES);
    memcpy(buf, definition.c_str() + lines[i].start, len);
    buf[len] = '\0';
    renderer.drawText(fontId, x, startY + (i - firstLine) * lineHeight, buf);
  }
}

void DictionaryDefinitionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = contentRect();

  // Header: back button (touch boards), matched headword, page counter right.
  char counter[16];
  snprintf(counter, sizeof(counter), "%d/%d", currentPage + 1, totalPages);
  GUI.drawHeader(renderer, Rect{content.x, content.y + metrics.topPadding, content.width, metrics.headerHeight},
                 headword.c_str(), totalPages > 1 ? counter : nullptr);

  const Rect tabBar = tabBarRect();
  if (showTabs()) drawTabs(tabBar);

  const int bodyX = content.x + SIDE_PADDING;
  const int bodyStartY = bodyTop();
  if (hasMessage) {
    // I18N.get directly: tr() only accepts literal key names.
    renderer.drawText(UI_12_FONT_ID, bodyX, bodyStartY + metrics.verticalSpacing, I18N.get(message));
  } else {
    // Body: two-pass draw inside a prewarm scope (same pattern as the reader's
    // renderContents) so SD-card font glyphs load from SD in one batch instead
    // of one on-demand overflow read per character on every page turn.
    const int fontId = SETTINGS.getReaderFontId();
    auto* fcm = renderer.getFontCacheManager();
    auto scope = fcm->createPrewarmScope();
    drawBody(fontId, bodyX, bodyStartY);  // scan pass: records codepoints only
    scope.endScanAndPrewarm();
    drawBody(fontId, bodyX, bodyStartY);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tabs.size() > 1 ? tr(STR_DICTIONARY) : "",
                                            (currentPage > 0 ? "<" : ""), (currentPage + 1 < totalPages ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  if (busy) {
    // drawPopup overlays the framebuffer and refreshes the display itself.
    GUI.drawPopup(renderer, I18N.get(busyMessage));
    return;
  }
  renderer.displayBuffer();
}
