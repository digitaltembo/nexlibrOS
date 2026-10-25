#pragma once
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "lua/LuaRuntime.h"

// True when /apps holds at least one app directory with an index.lua.
bool anyLuaAppInstalled();

// A "lookupHook:<fn>" permission of an installed app.
struct LuaLookupHook {
  char app[luapaths::MAX_APP_NAME + 1];  // app directory name
  char function[luapaths::MAX_HOOK_NAME + 1];
};

// Fills hooks with up to maxHooks lookup hooks declared by installed apps, in
// directory order; returns how many were found.
size_t findLuaLookupHooks(LuaLookupHook* hooks, size_t maxHooks);

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
