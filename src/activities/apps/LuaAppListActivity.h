#pragma once
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

// True when /apps holds at least one app directory with an index.lua.
bool anyLuaAppInstalled();

// Lists the Lua apps in /apps (one directory per app, entry script index.lua)
// and launches the selected one in LuaAppActivity. Row titles come from an
// optional "-- title: ..." / "-- description: ..." comment header in the
// first lines of index.lua, falling back to the directory name.
class LuaAppListActivity final : public UiListActivity {
 public:
  explicit LuaAppListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;

 private:
  struct AppEntry {
    std::string name;  // app directory name
    std::string path;  // entry script
    std::string title;
    std::string description;
  };

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  void scanApps();

  std::vector<AppEntry> apps;
  std::vector<freeink::ui::ListItem> rowItems;
};
