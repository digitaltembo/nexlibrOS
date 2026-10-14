#pragma once

#include <Epub/Page.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"
#include "util/DictionaryLookup.h"
#include "util/DictionaryRegistry.h"

// Paged viewer for one dictionary definition. HTML definitions are laid out
// through the EPUB chapter parser into styled Pages; anything else (plain
// text, or HTML too damaged to parse) is word-wrapped once on entry and each
// page renders spans of the original string, so no per-line copies are held.
// For a dictionary lookup, the definition shown first comes from the first
// dictionary that had the word (the book's language first); a background task
// then looks the word up in the remaining ones and each that has it gets a tab
// (tap a tab, or Confirm to cycle). Tab results stay in RAM within RESULT_CACHE_BYTES, so
// switching tabs needs no further lookup.
class DictionaryDefinitionActivity final : public Activity {
 public:
  // Plain titled text viewer (e.g. a plugin README): no dictionary tabs.
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition = false)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        headword(std::move(headword)),
        definition(std::move(definition)),
        htmlDefinition(htmlDefinition) {}

  // Dictionary lookup. `lookupWord` is the text that was looked up.
  // `dictionaries` are the dictionaries still to consult, the one that
  // produced `definition` first; the rest are looked up in the background.
  // `dictLookup` is the caller's open dictionary, used only to re-read a tab
  // whose definition was too large to keep; it must outlive this activity.
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        DictionaryLookup& dictLookup, std::string lookupWord,
                                        std::vector<DictionaryEntry> dictionaries, std::string headword,
                                        std::string definition, bool htmlDefinition = false)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        dictLookup(&dictLookup),
        lookupWord(std::move(lookupWord)),
        dictionaries(std::move(dictionaries)),
        tabBand(this->dictionaries.size() > 1),
        headword(std::move(headword)),
        definition(std::move(definition)),
        htmlDefinition(htmlDefinition) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One wrapped display line: a byte span of `definition`. Wrapping keeps
  // lines under the screen width, so uint16_t length is ample.
  struct Line {
    uint32_t start;
    uint16_t len;
  };

  // Usable body-text area between the header and the button hints.
  struct BodyArea {
    int width;
    int height;
  };

  // One dictionary's lookup result. Every tab holds a hit.
  struct DictResult {
    std::string name;
    std::string headword;
    // Raw definition, held so selecting the tab is instant; empty (retained
    // false) when keeping it would exceed RESULT_CACHE_BYTES.
    std::string definition;
    bool found = false;
    bool html = false;
    bool retained = false;
  };

  // Combined size of the definitions held by tabs.
  static constexpr size_t RESULT_CACHE_BYTES = 24 * 1024;

  // The band is reserved whenever more than one dictionary is consulted, so
  // tabs arriving from the background never reflow the visible definition.
  bool showTabs() const { return tabBand; }
  bool searching() const { return bgCollected < bgSlots.size(); }
  // Tabs plus a trailing "..." placeholder while lookups are still running.
  int slotCount() const { return static_cast<int>(tabs.size()) + (searching() ? 1 : 0); }
  const char* slotLabel(int index) const;
  // Content column (right of a landscape hint gutter, below an inverted hint
  // bar) and the tab band inside it.
  Rect contentRect() const;
  Rect tabBarRect() const;
  void tabSlot(const Rect& bar, int index, int& x, int& width) const;
  int tabAt(int x, int y) const;
  // Y of the first body line, below the header and any tab band.
  int bodyTop() const;
  void switchTab(int index);
  void showTab(int index);
  void layoutDefinition();
  void clearContent();

  void startBackgroundLookups();
  void stopBackgroundLookups();
  // Moves results the task has published into tabs and reaps the finished
  // task. True when the tab band changed.
  bool collectBackgroundResults();
  static void backgroundTrampoline(void* param);
  void backgroundMain();

  BodyArea bodyArea() const;
  bool layoutHtmlPages();
  void wrapText();
  int measureSpan(int fontId, const char* text, size_t len) const;
  void drawBody(int fontId, int x, int startY) const;
  void drawTabs(Rect rect) const;

  // Null for the plain text viewer, which has no tabs.
  DictionaryLookup* dictLookup = nullptr;
  // Not `word`: Arduino.h defines a word() macro.
  const std::string lookupWord;
  // Consumed by onEnter() into tabs and bgSlots.
  std::vector<DictionaryEntry> dictionaries;
  const bool tabBand = false;
  std::vector<DictResult> tabs;
  int activeTab = 0;
  // Unretained tab chosen while the task runs: shown once it has finished, so
  // two lookups never compete for the heap or the shared dictzip temp file.
  int deferredTab = -1;

  // Background lookups, one slot per remaining dictionary. The task fills
  // slot i, then publishes it by raising bgPublished past i; from then on only
  // loop() touches it. bgCacheBudget is the task's share of RESULT_CACHE_BYTES.
  std::vector<DictResult> bgSlots;
  size_t bgCacheBudget = 0;
  size_t bgCollected = 0;
  std::atomic<size_t> bgPublished{0};
  std::atomic<bool> bgCancel{false};
  std::unique_ptr<DictionaryLookup> bgLookup;
  TaskHandle_t bgTask = nullptr;
  SemaphoreHandle_t bgDone = nullptr;

  std::string headword;
  // onEnter() normalizes embedded NULs (StarDict multi-type separators) to
  // newlines so C-string APIs see the whole text.
  std::string definition;
  bool htmlDefinition;
  // Set when the active tab's definition could not be re-read: the body shows
  // this message instead.
  bool hasMessage = false;
  StrId message = StrId::STR_DICT_NOT_FOUND;
  // "Looking up…" / "Indexing…" popup while a tab switch waits on SD.
  bool busy = false;
  StrId busyMessage = StrId::STR_DICT_LOOKING_UP;
  // Styled path: reader-identical Pages laid out from the HTML definition.
  // Empty means the plain-text span path below is active.
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<Line> lines;
  int currentPage = 0;
  int totalPages = 1;
  int linesPerPage = 1;
  ButtonNavigator buttonNavigator;
};
