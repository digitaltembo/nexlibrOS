#pragma once

#include <array>
#include <memory>

#include "activities/UiListActivity.h"
#include "clippings/ClippingPreview.h"
#include "components/OptionPopup.h"

class EpubReaderClippingListActivity final : public UiListActivity {
 public:
  EpubReaderClippingListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  void rebuildRows();
  void refreshRowWindow(int start);
  // Store index of a list row; rows are in reading order.
  size_t storeIndex(int row) const;
  void openSelected();
  void showActions();
  void readSelected();
  void showDeleteConfirmation();
  void deleteSelected();

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  const char* headerTitle() const override;

  static constexpr int ROW_WINDOW = 8;
  std::array<std::array<char, clippingPreview::BUFFER_BYTES>, ROW_WINDOW> previews{};
  std::array<size_t, ROW_WINDOW> previewLengths{};
  std::array<freeink::ui::ListItem, ROW_WINDOW> rowItems;
  std::unique_ptr<uint16_t[]> order;
  int windowStart = -1;
  int windowCount = 0;
  OptionPopup confirmPopup;
  bool confirmingDelete = false;
  bool initialListRender = true;
};
