#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "HighlightMarks.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DictionaryRegistry.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;

// Highlight box grows the word by this much on each side.
constexpr int HIGHLIGHT_PAD = 2;

// Action bubble geometry.
constexpr int BUBBLE_FONT_ID = UI_10_FONT_ID;
constexpr int BUBBLE_PAD_X = 12;
constexpr int BUBBLE_PAD_Y = 6;
constexpr int BUBBLE_RADIUS = 6;
constexpr int BUBBLE_GAP = 3;  // highlight edge to arrow tip
constexpr int POINTER_HEIGHT = 7;
constexpr int POINTER_HALF_WIDTH = 7;
constexpr int BUBBLE_HIT_SLOP = 6;

// A token is selectable when it has an ASCII alphanumeric or a non-ASCII
// codepoint outside U+2000-U+206F (dashes, bullets and other General
// Punctuation that appear as standalone tokens are not words).
bool isSelectableToken(const char* text) {
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(text); *p != 0; p++) {
    if (*p < 0x80) {
      if (std::isalnum(*p)) return true;
    } else if (*p == 0xE2 && (p[1] == 0x80 || p[1] == 0x81)) {
      if (p[2] == 0) break;  // truncated sequence: skipping would step past the NUL
      p += 2;                // skip the 3-byte General Punctuation codepoint
    } else {
      return true;
    }
  }
  return false;
}

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  // No null check: a failed allocation just disables the differential
  // fast path (drawHighlightWithSnapshot skips the read), keeping the
  // full-repaint path as the fallback.
  snapshot = makeUniqueNoThrow<uint8_t[]>(SNAPSHOT_CAPACITY);
  prepareWords();
  // Start on the middle row's word nearest mid-screen instead of top-left:
  // any word on the page is then at most half a page of moves away.
  if (!preselected && !words.empty()) {
    const int initial = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
    if (initial >= 0) selected = initial;
  }
  requestUpdate();
}

bool DictionaryWordSelectActivity::selectWordAt(const int x, const int y) {
  prepareWords();
  const int hit = wordAt(x, y);
  if (hit < 0) return false;
  selected = hit;
  preselected = true;
  return true;
}

void DictionaryWordSelectActivity::prepareWords() {
  if (wordsReady) return;
  wordsReady = true;
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  extractWords();
}

void DictionaryWordSelectActivity::extractWords() {
  words.clear();
  words.reserve(128);
  rowCount = 0;

  // Single walk: collect the selectable words while accumulating their text
  // and styles (~2KB transient string, freed on return). Widths are measured
  // afterwards: merging the page's codepoints into the SD font's persistent
  // advance table first keeps getTextAdvanceX on the in-RAM path instead of
  // loading glyphs from SD one overflow slot at a time.
  std::string pageText;
  pageText.reserve(2048);
  uint8_t styleMask = 0;

  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto* line = static_cast<const PageLine*>(element.get());
    const auto* block = line->getBlock();
    if (!block || !block->valid()) continue;

    bool rowHasWords = false;
    const int ascender = renderer.getFontAscenderSize(fontId);
    const int rubyShift = block->getRubyShift(ascender);
    for (uint16_t i = 0; i < block->wordCount(); i++) {
      const char* text = block->wordText(i);
      if (!isSelectableToken(text)) continue;

      WordBox box;
      box.x = static_cast<int16_t>(line->xPos + block->wordXpos(i) + marginLeft);
      box.y = static_cast<int16_t>(line->yPos + marginTop + rubyShift);
      box.style = block->wordStyle(i);
      box.width = 0;  // measured below, once the advance table is ready
      box.row = rowCount;
      box.offset = block->wordVisibleOffset(i);
      box.text = text;
      words.push_back(box);
      rowHasWords = true;

      pageText.append(text);
      pageText.push_back(' ');
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(box.style) & 0x03));
    }
    if (rowHasWords) rowCount++;
  }

  if (styleMask == 0) styleMask = 0x01;  // REGULAR
  renderer.ensureSdCardFontReady(fontId, pageText.c_str(), styleMask);
  for (auto& word : words) {
    word.width = static_cast<int16_t>(renderer.getTextAdvanceX(fontId, word.text, word.style));
  }
}

// Index of the word whose box (with finger-sized slop) contains the touch
// point; -1 when the touch lands on no word. Boxes never overlap after the
// slop grows them, at worst they touch, so first hit wins.
int DictionaryWordSelectActivity::wordAt(const int x, const int y) const {
  constexpr int SLOP = 4;  // matches the highlight box (+2) plus finger error
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    const WordBox& word = words[i];
    if (x >= word.x - SLOP && x < word.x + word.width + SLOP && y >= word.y - SLOP && y < word.y + lineHeight + SLOP) {
      return i;
    }
  }
  return -1;
}

// Index of the word in `row` whose horizontal center is closest to centerX;
// -1 when the row has no words.
int DictionaryWordSelectActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(words.size()); i++) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

void DictionaryWordSelectActivity::moveVertical(const int direction) {
  const WordBox& current = words[selected];
  const int targetRow = static_cast<int>(current.row) + direction;
  if (targetRow < 0 || targetRow >= static_cast<int>(rowCount)) return;

  const int best = closestInRow(static_cast<uint16_t>(targetRow), current.x + current.width / 2);
  if (best >= 0 && best != selected) {
    selected = best;
    requestUpdate();
  }
}

void DictionaryWordSelectActivity::performLookup() {
  popup = Popup::Busy;
  popupMsg = dictLookup.prepare(SETTINGS.dictionaryName) ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  const char* text = words[selected].text;
  std::string definition;
  std::string headword;
  // The default dictionary's failure is the one reported if no dictionary has
  // the word.
  StrId error = StrId::STR_DICT_NOT_FOUND;
  std::string foundIn;
  if (dictLookup.lookup(text, definition, headword, error)) {
    foundIn = SETTINGS.dictionaryName;
  } else {
    // Miss in the default dictionary: try every other installed one before
    // reporting it.
    std::vector<DictionaryEntry> dictionaries;
    DictionaryRegistry::discover(dictionaries);
    for (const auto& entry : dictionaries) {
      if (entry.name == SETTINGS.dictionaryName) continue;
      if (dictLookup.prepare(entry.name.c_str()) && popupMsg != StrId::STR_DICT_INDEXING) {
        popupMsg = StrId::STR_DICT_INDEXING;
        requestUpdateAndWait();
      }
      StrId otherError = StrId::STR_DICT_NOT_FOUND;
      if (dictLookup.lookup(text, definition, headword, otherError)) {
        foundIn = entry.name;
        break;
      }
    }
  }

  if (!foundIn.empty()) {
    popup = Popup::None;
    // Back from the definition returns straight to the reader.
    startActivityForResult(std::make_unique<DictionaryDefinitionActivity>(
                               renderer, mappedInput, dictLookup, text, foundIn.c_str(), std::move(headword),
                               std::move(definition), dictLookup.definitionsAreHtml()),
                           [](const ActivityResult&) { finish(); });
    return;
  }
  popup = error == StrId::STR_DICT_NOT_FOUND ? Popup::NotFound : Popup::Error;
  popupMsg = error;
  popupTime = millis();
  requestUpdate();
}

int DictionaryWordSelectActivity::highlightAt(const int wordIdx) const {
  for (int i = 0; i < static_cast<int>(highlights.size()); i++) {
    if (highlights[i].contains(spineIndex, words[wordIdx].offset)) return i;
  }
  return -1;
}

const PageLink* DictionaryWordSelectActivity::linkAt(const int wordIdx) const {
  // Probe the word's center, without the finger slop the reader's direct link
  // taps use, so a neighbor of a narrow note marker doesn't claim its link.
  const WordBox& word = words[wordIdx];
  const int pageX = word.x + word.width / 2 - marginLeft;
  const int pageY = word.y + lineHeight / 2 - marginTop;
  for (const auto& link : page->links) {
    if (link.contains(pageX, pageY, 0, 0)) return &link;
  }
  return nullptr;
}

StrId DictionaryWordSelectActivity::primaryAction() const {
  if (anchor >= 0) return StrId::STR_DONE;
  if (mode == Mode::Lookup && lookupAvailable) return StrId::STR_LOOKUP;
  return highlightAt(selected) >= 0 ? StrId::STR_REMOVE_HIGHLIGHT : StrId::STR_HIGHLIGHT;
}

void DictionaryWordSelectActivity::runAction(const StrId action) {
  switch (action) {
    case StrId::STR_LOOKUP:
      performLookup();
      break;
    case StrId::STR_HIGHLIGHT:
      anchor = selected;
      requestUpdate();
      break;
    case StrId::STR_REMOVE_HIGHLIGHT:
      removeHighlight(highlightAt(selected));
      break;
    case StrId::STR_DONE:
      saveRange();
      break;
    case StrId::STR_GO_TO:
      if (const PageLink* link = linkAt(selected)) {
        followHref = link->href;
        finish();
      }
      break;
    default:
      break;
  }
}

void DictionaryWordSelectActivity::saveRange() {
  const int lo = std::min(anchor, selected);
  const int hi = std::max(anchor, selected);

  HighlightEntry entry;
  entry.spineIndex = spineIndex;
  entry.start = words[lo].offset;
  entry.end = words[hi].offset;
  for (const auto* p = reinterpret_cast<const uint8_t*>(words[hi].text); *p != 0; p++) {
    if ((*p & 0xC0) != 0x80) entry.end++;  // one per UTF-8 lead byte
  }
  for (int i = lo; i <= hi; i++) {
    const size_t len = strlen(words[i].text);
    if (entry.text.size() + len + 1 > HighlightEntry::MAX_TEXT_LENGTH) break;
    if (i > lo) entry.text.push_back(' ');
    entry.text.append(words[i].text, len);
  }

  // Absorb any saved highlight the new one overlaps or touches (the merged
  // entry keeps the new selection's text).
  for (auto it = highlights.begin(); it != highlights.end();) {
    if (it->spineIndex == spineIndex && it->start <= entry.end && entry.start <= it->end) {
      entry.start = std::min(entry.start, it->start);
      entry.end = std::max(entry.end, it->end);
      it = highlights.erase(it);
    } else {
      ++it;
    }
  }
  highlights.push_back(std::move(entry));
  HighlightFile::save(bookPath, highlights);
  finish();
}

void DictionaryWordSelectActivity::removeHighlight(const int index) {
  if (index < 0 || index >= static_cast<int>(highlights.size())) return;
  highlights.erase(highlights.begin() + index);
  HighlightFile::save(bookPath, highlights);
  finish();
}

void DictionaryWordSelectActivity::loop() {
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      popup = Popup::None;
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (anchor >= 0) {
      anchor = -1;  // abandon the highlight in progress, keep the cursor
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && !words.empty()) {
    runAction(primaryAction());
    return;
  }

  if (words.empty()) return;

  // Touch: the first tap (or long-press) on a word moves the cursor to it, a
  // tap on the cursor word or a bubble segment runs that action, and a tap
  // away from the text leaves.
  int tx = 0;
  int ty = 0;
  if (mappedInput.wasScreenLongPress(tx, ty)) {
    const int hit = wordAt(tx, ty);
    if (hit >= 0 && hit != selected) {
      selected = hit;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    // The bubble overlaps the neighbouring line, so it takes the tap first.
    const int segment = bubbleHit(tx, ty);
    if (segment >= 0) {
      runAction(lookupBubble().labels[segment]);
      return;
    }
    const int hit = wordAt(tx, ty);
    if (hit < 0) {
      finish();
    } else if (hit == selected) {
      runAction(primaryAction());
    } else {
      selected = hit;
      requestUpdate();
    }
    return;
  }

  const bool hasNextWord = selected + 1 < static_cast<int>(words.size());
  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= WORD_REPEAT_START_MS && now - lastHorizontalMoveTime >= WORD_REPEAT_INTERVAL_MS;
  const bool moveLeft = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                        (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool moveRight = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if (moveLeft && selected > 0) {
    selected--;
    lastHorizontalMoveTime = now;
    requestUpdate();
  } else if (moveRight && hasNextWord) {
    selected++;
    lastHorizontalMoveTime = now;
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

// Inverts words[lo..hi] as one bar per row, spanning the gaps between words.
void DictionaryWordSelectActivity::drawSelection(const int lo, const int hi) const {
  int rowStart = lo;
  while (rowStart <= hi) {
    int rowEnd = rowStart;
    int left = words[rowStart].x;
    int right = words[rowStart].x + words[rowStart].width;
    while (rowEnd + 1 <= hi && words[rowEnd + 1].row == words[rowStart].row) {
      rowEnd++;
      left = std::min<int>(left, words[rowEnd].x);
      right = std::max<int>(right, words[rowEnd].x + words[rowEnd].width);
    }
    renderer.fillRect(left - HIGHLIGHT_PAD, words[rowStart].y - HIGHLIGHT_PAD, right - left + 2 * HIGHLIGHT_PAD,
                      lineHeight + 2 * HIGHLIGHT_PAD, true);
    for (int i = rowStart; i <= rowEnd; i++) {
      renderer.drawText(fontId, words[i].x, words[i].y, words[i].text, false, words[i].style);
    }
    rowStart = rowEnd + 1;
  }
}

// Saves the pixels under words[selected]'s highlight box (and bubble), then
// draws them. Returns false when the pixels could not be saved (no buffer /
// oversize box, or a multi-word selection) — everything is drawn regardless,
// but the next cursor move must do a full repaint.
bool DictionaryWordSelectActivity::drawHighlightWithSnapshot() {
  if (anchor >= 0) {
    // Whole selection inverted; too large to snapshot.
    drawSelection(std::min(anchor, selected), std::max(anchor, selected));
    bubbleSaved = false;
    if (showBubble) drawBubble(lookupBubble());
    snapshotIdx = -1;
    return false;
  }

  const auto region = [](const int x, const int y, const int w, const int h, const size_t offset) {
    SavedRegion r;
    r.x = static_cast<int16_t>(x);
    r.y = static_cast<int16_t>(y);
    r.w = static_cast<int16_t>(w);
    r.h = static_cast<int16_t>(h);
    r.offset = static_cast<uint16_t>(offset);
    return r;
  };

  const WordBox& word = words[selected];
  int hx = word.x - HIGHLIGHT_PAD;
  int hy = word.y - HIGHLIGHT_PAD;
  int hw = word.width + 2 * HIGHLIGHT_PAD;
  int hh = lineHeight + 2 * HIGHLIGHT_PAD;
  // Clamp to the panel so save, draw and restore all use the same box.
  if (hx < 0) {
    hw += hx;
    hx = 0;
  }
  if (hy < 0) {
    hh += hy;
    hy = 0;
  }

  size_t used = 0;
  if (snapshot && hw > 0 && hh > 0) {
    used = renderer.readFramebufferRegion(hx, hy, hw, hh, snapshot.get(), SNAPSHOT_CAPACITY);
  }
  bool saved = used > 0;
  highlightRegion = region(hx, hy, hw, hh, 0);

  renderer.fillRect(hx, hy, hw, hh, true);
  renderer.drawText(fontId, word.x, word.y, word.text, false, word.style);

  bubbleSaved = false;
  if (showBubble) {
    const Bubble bubble = lookupBubble();
    const int by = bubble.below ? bubble.y - POINTER_HEIGHT : bubble.y;
    const int bh = bubble.height + POINTER_HEIGHT;
    // Saved after the highlight is drawn: the two byte-aligned regions can
    // overlap, and restoring in reverse order unwinds them correctly.
    if (saved) {
      bubbleSaved = renderer.readFramebufferRegion(bubble.x, by, bubble.width, bh, snapshot.get() + used,
                                                   SNAPSHOT_CAPACITY - used) > 0;
      saved = bubbleSaved;
    }
    bubbleRegion = region(bubble.x, by, bubble.width, bh, used);
    drawBubble(bubble);
  }

  snapshotIdx = saved ? selected : -1;
  return saved;
}

DictionaryWordSelectActivity::Bubble DictionaryWordSelectActivity::lookupBubble() const {
  const WordBox& word = words[selected];
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);

  Bubble bubble;
  bubble.labelCount = 0;
  const StrId primary = primaryAction();
  if (primary == StrId::STR_LOOKUP) {
    // Lookup mode offers highlighting alongside.
    bubble.labels[bubble.labelCount++] = StrId::STR_LOOKUP;
    bubble.labels[bubble.labelCount++] =
        highlightAt(selected) >= 0 ? StrId::STR_REMOVE_HIGHLIGHT : StrId::STR_HIGHLIGHT;
  } else {
    bubble.labels[bubble.labelCount++] = primary;
  }
  // A linked word can be followed from here, so links under the bookmark
  // corner's tap target stay reachable.
  if (anchor < 0 && linkAt(selected)) bubble.labels[bubble.labelCount++] = StrId::STR_GO_TO;

  // Segment offsets are relative until bubble.x is known below.
  bubble.width = 0;
  for (int i = 0; i < bubble.labelCount; i++) {
    bubble.segmentX[i] = bubble.width;
    bubble.width += renderer.getTextWidth(BUBBLE_FONT_ID, I18N.get(bubble.labels[i])) + 2 * BUBBLE_PAD_X;
  }
  bubble.height = renderer.getLineHeight(BUBBLE_FONT_ID) + 2 * BUBBLE_PAD_Y;

  // A multi-word selection points at its first word from above, or at its
  // last word from below, so the bubble never covers selected text.
  const WordBox& first = anchor >= 0 ? words[std::min(anchor, selected)] : word;
  const WordBox& last = anchor >= 0 ? words[std::max(anchor, selected)] : word;
  const int aboveY = first.y - HIGHLIGHT_PAD - BUBBLE_GAP - POINTER_HEIGHT - bubble.height;
  bubble.below = aboveY < top;
  const WordBox& target = bubble.below ? last : first;
  bubble.y = bubble.below ? target.y + lineHeight + HIGHLIGHT_PAD + BUBBLE_GAP + POINTER_HEIGHT : aboveY;

  const int centerX = target.x + target.width / 2;
  const int maxX = renderer.getScreenWidth() - right - bubble.width;
  bubble.x = std::max(left, std::min(centerX - bubble.width / 2, maxX));

  // Keep the arrow on the bubble's straight edge when the bubble is pushed
  // sideways by the screen edge.
  const int inset = BUBBLE_RADIUS + POINTER_HALF_WIDTH;
  bubble.pointerX = std::max(bubble.x + inset, std::min(centerX, bubble.x + bubble.width - inset));
  for (int i = 0; i < bubble.labelCount; i++) bubble.segmentX[i] += bubble.x;
  return bubble;
}

// Index of the bubble segment under the touch point, or -1.
int DictionaryWordSelectActivity::bubbleHit(const int x, const int y) const {
  if (!showBubble || words.empty()) return -1;
  const Bubble bubble = lookupBubble();
  const bool inside = x >= bubble.x - BUBBLE_HIT_SLOP && x < bubble.x + bubble.width + BUBBLE_HIT_SLOP &&
                      y >= bubble.y - BUBBLE_HIT_SLOP && y < bubble.y + bubble.height + BUBBLE_HIT_SLOP;
  if (!inside) return -1;
  int segment = 0;
  while (segment + 1 < bubble.labelCount && x >= bubble.segmentX[segment + 1]) segment++;
  return segment;
}

void DictionaryWordSelectActivity::drawBubble(const Bubble& bubble) const {
  renderer.fillRoundedRect(bubble.x, bubble.y, bubble.width, bubble.height, BUBBLE_RADIUS, Color::Black);

  const int baseY = bubble.below ? bubble.y : bubble.y + bubble.height - 1;
  const int tipY = bubble.below ? bubble.y - POINTER_HEIGHT : bubble.y + bubble.height - 1 + POINTER_HEIGHT;
  const int xs[3] = {bubble.pointerX - POINTER_HALF_WIDTH, bubble.pointerX + POINTER_HALF_WIDTH, bubble.pointerX};
  const int ys[3] = {baseY, baseY, tipY};
  renderer.fillPolygon(xs, ys, 3, true);

  for (int i = 0; i < bubble.labelCount; i++) {
    const int segX = bubble.segmentX[i];
    if (i > 0) {
      renderer.drawLine(segX, bubble.y + BUBBLE_PAD_Y, segX, bubble.y + bubble.height - BUBBLE_PAD_Y, false);
    }
    renderer.drawText(BUBBLE_FONT_ID, segX + BUBBLE_PAD_X, bubble.y + BUBBLE_PAD_Y, I18N.get(bubble.labels[i]), false);
  }
}

// Front-button bar (Back/Confirm/Left/Right). Drawn last on every repaint
// path, including the differential highlight-only path, so it always ends
// up as the top layer even when a highlighted word's box falls under a
// hint's screen area. No side-button hints: the full-bleed reader page has no
// spare gutter for them, so a hint box there would hide text.
void DictionaryWordSelectActivity::drawHints() const {
  // No selectable word on this page: Confirm and navigation are all no-ops
  // (guarded by words.empty() in loop()/performLookup), so only Back does
  // anything and only Back is hinted.
  if (words.empty()) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  // Short labels on the hint bar: Select starts a highlight, Delete removes one.
  StrId confirm = primaryAction();
  if (confirm == StrId::STR_HIGHLIGHT) {
    confirm = StrId::STR_SELECT;
  } else if (confirm == StrId::STR_REMOVE_HIGHLIGHT) {
    confirm = StrId::STR_DELETE;
  }
  const auto labels = mappedInput.mapDirectionalLabels(tr(STR_BACK), I18N.get(confirm), tr(STR_DIR_LEFT),
                                                       tr(STR_DIR_RIGHT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  // Differential fast path: only the highlight moved and the framebuffer
  // still holds a clean page (no popup or sub-activity since the last full
  // repaint). Restore the pixels under the old highlight, draw the new one,
  // and push — skipping the two-pass page render entirely.
  if (popup == Popup::None && snapshotIdx >= 0 && !words.empty() && selected != snapshotIdx) {
    if (bubbleSaved) {
      renderer.writeFramebufferRegion(bubbleRegion.x, bubbleRegion.y, bubbleRegion.w, bubbleRegion.h,
                                      snapshot.get() + bubbleRegion.offset);
    }
    renderer.writeFramebufferRegion(highlightRegion.x, highlightRegion.y, highlightRegion.w, highlightRegion.h,
                                    snapshot.get());
    // The full path's PrewarmScope cleared the glyph cache on exit; batch-load
    // just the highlighted word's glyphs before drawing them white-on-black.
    renderer.getFontCacheManager()->prewarmCache(
        fontId, words[selected].text, static_cast<uint8_t>(1u << (static_cast<uint8_t>(words[selected].style) & 0x03)));
    if (drawHighlightWithSnapshot()) {
      drawHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    }
    // Snapshot failed (oversize box) — fall through to a full repaint.
  }

  renderer.clearScreen();

  // Same prewarm-scan-then-render pass the reader uses, so SD-card fonts hit
  // the in-RAM glyph cache during the real draw.
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  HighlightMarks::drawBackgrounds(renderer, *page, fontId, marginLeft, marginTop, highlights, spineIndex);
  page->render(renderer, fontId, marginLeft, marginTop);

  if (!words.empty()) {
    drawHighlightWithSnapshot();
  }

  drawHints();

  if (popup != Popup::None) {
    // The popup overdraws the page, so the snapshot no longer matches the
    // framebuffer — force the next render onto the full-repaint path.
    snapshotIdx = -1;
    // drawPopup overlays the framebuffer and refreshes the display itself.
    // I18N.get directly: tr() only accepts literal key names.
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
