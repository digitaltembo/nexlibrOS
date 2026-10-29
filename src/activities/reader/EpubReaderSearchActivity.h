#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "BookPageEstimate.h"
#include "BookTextIndex.h"
#include "BookTextSearch.h"
#include "activities/UiListActivity.h"

// Full-text search of the open book. Builds the book's text index on first
// use, scans it a slice per loop pass (progress on screen, Back cancels), then
// lists every match with its page and a two-line snippet. A tap finishes with
// a ProgressChangeResult at the match's visible-text offset.
class EpubReaderSearchActivity final : public UiListActivity {
 public:
  // `pages`, `currentSpine` and `currentChapterPages` number the results the
  // way the reader's toolbar numbers pages.
  explicit EpubReaderSearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::shared_ptr<Epub>& epub, std::string term, const BookPageEstimate& pages,
                                    int currentSpine, int currentChapterPages);
  void onEnter() override;
  void loop() override;

 private:
  enum class State { Indexing, Searching, Done, Failed };

  int listCount() const override { return state == State::Done ? static_cast<int>(rowItems.size()) : 0; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleButtons() override;
  // The base render() lays the wrapped rows out again until the selection
  // fits on screen; only the title and hints are this screen's own.
  const char* headerTitle() const override;
  void drawFooter() override;

  void startSearch();
  void finishSearch();
  void setProgress(int percent);

  std::shared_ptr<Epub> epub;
  std::string term;
  BookPageEstimate pages;
  int currentSpine;
  int currentChapterPages;

  State state = State::Indexing;
  int progress = 0;  // percent of the current phase, as last drawn
  int nextSpine = 0;
  std::unique_ptr<BookTextIndex::Builder> builder;  // only while indexing
  BookTextSearch search;

  std::string title;
  std::vector<std::string> rowSubtitles;
  std::vector<freeink::ui::ListItem> rowItems;
};
