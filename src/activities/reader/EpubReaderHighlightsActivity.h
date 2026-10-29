#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/HighlightFile.h"

// The book's highlights in reading order: a tap jumps to one, a hold offers
// Open / Delete. Finishes with a ProgressChangeResult carrying the highlight's
// spine and visible-text offset, the same shape the bookmarks list returns.
class EpubReaderHighlightsActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::vector<HighlightEntry> highlights;
  // Row buffers derived from `highlights`, rebuilt only when it changes.
  std::vector<std::string> highlightSubtitles;
  std::vector<freeink::ui::ListItem> highlightRowItems;
  void rebuildHighlightRowItems();
  bool confirmingDelete = false;
  OptionPopup confirmPopup;

 public:
  explicit EpubReaderHighlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const std::shared_ptr<Epub>& epub, const std::string& epubPath);
  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(highlights.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;

  void openSelectedHighlight();
  void showHighlightActions();
  void showDeleteConfirmation();
  void deleteSelectedHighlight();
};
