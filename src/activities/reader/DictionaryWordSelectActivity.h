#pragma once

#include <BoardConfig.h>
#include <Epub/Page.h>
#include <I18n.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/apps/LuaAppListActivity.h"
#include "util/DictionaryLookup.h"

// Word selection over the current reader page: Left/Right step through words
// in reading order, Up/Down jump rows, Back returns to the reader. Confirm looks
// the word up and opens DictionaryDefinitionActivity.
//
// On touch devices a tap on another word moves the cursor and the bubble over
// it offers Look Up and Highlight (or Remove Highlight), then the lookup hooks
// of installed Lua apps, which open the app on the word. Highlight hands the
// word to the reader, which opens ClipSelectionActivity on it.
class DictionaryWordSelectActivity final : public Activity {
 public:
  // Where the page sits, for matching words against saved highlights.
  struct PageContext {
    uint16_t spineIndex;
    uint16_t page;
    uint16_t pageCount;
    uint32_t layoutSignature;
  };

  // What the reader should do once this activity finishes.
  struct Outcome {
    std::string followHref;  // Go to: the link to follow
    bool startHighlight = false;
    int highlightX = 0;  // screen point of the word to start highlighting from
    int highlightY = 0;
  };

  // Lookups favour dictionaries serving `bookLanguage` (the EPUB's dc:language)
  // over the rest. `drawHighlights` paints saved highlights under the page text.
  explicit DictionaryWordSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        std::unique_ptr<Page> page, int marginLeft, int marginTop, bool lookupAvailable,
                                        std::string bookLanguage, const PageContext& context, Outcome& outcome,
                                        std::function<void(const Page&)> drawHighlights)
      : Activity("DictionaryWordSelect", renderer, mappedInput),
        page(std::move(page)),
        marginLeft(marginLeft),
        marginTop(marginTop),
        lookupAvailable(lookupAvailable),
        bookLanguage(std::move(bookLanguage)),
        context(context),
        outcome(outcome),
        drawHighlights(std::move(drawHighlights)) {}

  // Pre-selects the word under a screen point (the reader's long-press) before
  // the activity starts. False when no word is there.
  bool selectWordAt(int x, int y);
  // Looks up `text` (e.g. a multi-word clip selection) as soon as the activity
  // starts, instead of waiting for a word to be chosen.
  void lookUpOnEnter(std::string text) { pendingLookup = std::move(text); }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Screen box of one selectable word. `text` points into the owned Page's
  // TextBlock arena (NUL-terminated), valid for this activity's lifetime.
  struct WordBox {
    int16_t x;
    int16_t y;
    int16_t width;
    uint16_t row;
    uint32_t offset;         // visible-text offset within the spine
    uint32_t endOffset;      // exclusive
    uint16_t pageWordIndex;  // position among the page's visible words
    const char* text;
    EpdFontFamily::Style style;
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  static constexpr int MAX_LOOKUP_HOOKS = 2;
  static constexpr int MAX_BUBBLE_LABELS = 3 + MAX_LOOKUP_HOOKS;

  // Tappable callout above the cursor word (or selection), or below it when it
  // sits too close to the top of the screen. pointerX is the tip of the arrow,
  // over the word's center. Segment i starts at segmentX[i] and ends where the
  // next one starts, the last at the bubble's right edge. A segment with
  // hooks[i] >= 0 runs lookupHooks[hooks[i]] instead of labels[i].
  struct Bubble {
    int x;
    int y;
    int width;
    int height;
    int pointerX;
    bool below;
    uint8_t labelCount;
    StrId labels[MAX_BUBBLE_LABELS];
    int8_t hooks[MAX_BUBBLE_LABELS];
    int segmentX[MAX_BUBBLE_LABELS];
  };

  // A framebuffer rectangle saved into `snapshot` at `offset`.
  struct SavedRegion {
    int16_t x = 0;
    int16_t y = 0;
    int16_t w = 0;
    int16_t h = 0;
    uint16_t offset = 0;
  };

  void prepareWords();
  void extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void performLookup(const char* text);
  bool drawHighlightWithSnapshot();
  Bubble lookupBubble() const;
  int bubbleHit(int x, int y) const;
  const char* bubbleLabel(const Bubble& bubble, int segment) const;
  void runBubbleSegment(int segment);
  void drawBubble(const Bubble& bubble) const;
  void drawHints() const;

  // Index in CLIPPINGS of the saved highlight covering words[wordIdx], or -1.
  int highlightAt(int wordIdx) const;
  // The page link that words[wordIdx] belongs to, or nullptr.
  const PageLink* linkAt(int wordIdx) const;
  // The action Confirm (or a tap on the cursor word) performs right now.
  StrId primaryAction() const;
  void runAction(StrId action);
  // Opens the hook's app with the selected word.
  void runLookupHook(int index);
  void removeHighlight(int index);

  std::unique_ptr<Page> page;
  const int marginLeft;
  const int marginTop;
  const bool lookupAvailable;
  const std::string bookLanguage;
  const PageContext context;
  Outcome& outcome;
  const std::function<void(const Page&)> drawHighlights;
  int fontId = 0;
  int lineHeight = 0;

  std::vector<WordBox> words;
  int selected = 0;
  bool wordsReady = false;
  bool preselected = false;
  std::string pendingLookup;  // see lookUpOnEnter()
  // Touch boards show a tappable bubble over the cursor.
  const bool showBubble = BoardConfig::hasTouch();
  LuaLookupHook lookupHooks[MAX_LOOKUP_HOOKS] = {};
  uint8_t lookupHookCount = 0;
  uint16_t rowCount = 0;
  unsigned long lastHorizontalMoveTime = 0;

  // Shared with the definition viewer, which may switch it to another
  // dictionary; prepare() reopens the configured one on the next lookup.
  DictionaryLookup dictLookup;

  Popup popup = Popup::None;
  StrId popupMsg = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupTime = 0;

  // Differential highlight repaint: the pixels under the current highlight
  // box (and bubble, when shown), so a cursor move restores them and repaints
  // only the affected boxes instead of re-running the full two-pass page
  // render (which also reloads every SD-font glyph on the page). snapshotIdx
  // is the word whose under-pixels are saved; -1 means the framebuffer no
  // longer holds a clean page (popup drawn, sub-activity shown, multi-word
  // selection) and the next render must be full. The bubble region is saved
  // after the highlight is drawn, so it is restored first.
  static constexpr size_t SNAPSHOT_CAPACITY = 4096;
  std::unique_ptr<uint8_t[]> snapshot;
  SavedRegion highlightRegion;
  SavedRegion bubbleRegion;
  bool bubbleSaved = false;
  int snapshotIdx = -1;
};
