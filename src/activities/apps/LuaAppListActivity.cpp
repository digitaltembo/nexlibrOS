#include "LuaAppListActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "LuaAppActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "lua/LuaRuntime.h"

namespace fui = freeink::ui;

namespace {
constexpr size_t MAX_APPS = 64;
// Room for "/apps/<name>" in LuaRuntime's app directory buffer.
constexpr size_t MAX_APP_NAME = 48;

// Writes the entry script path of app directory name into out; false when
// name is hidden (".lib"), too long, or has no entry script.
bool appEntryPath(const char* name, char* out, const size_t outLen) {
  const size_t len = strlen(name);
  if (len == 0 || len > MAX_APP_NAME || name[0] == '.') return false;
  const int written = snprintf(out, outLen, "%s/%s/%s", luapaths::APPS_DIR, name, luapaths::ENTRY_FILE);
  return written > 0 && static_cast<size_t>(written) < outLen && Storage.exists(out);
}
}  // namespace

bool anyLuaAppInstalled() {
  HalFile dir = Storage.open(luapaths::APPS_DIR);
  if (!dir || !dir.isDirectory()) return false;
  char name[64];
  char path[luapaths::MAX_PATH];
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) continue;
    if (entry.getName(name, sizeof(name)) > 0 && appEntryPath(name, path, sizeof(path))) return true;
  }
  return false;
}

LuaAppListActivity::LuaAppListActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("LuaAppList", renderer, mappedInput) {}

void LuaAppListActivity::onEnter() {
  UiListActivity::onEnter();
  scanApps();
  rowItems.clear();
  rowItems.reserve(apps.size());
  for (size_t i = 0; i < apps.size(); i++) {
    fui::ListItem row;
    row.label = apps[i].title.c_str();
    row.subtitle = apps[i].description.empty() ? nullptr : apps[i].description.c_str();
    row.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(row);
  }
  requestUpdate();
}

void LuaAppListActivity::scanApps() {
  apps.clear();
  apps.reserve(16);
  {
    HalFile dir = Storage.open(luapaths::APPS_DIR);
    if (!dir || !dir.isDirectory()) return;
    char name[64];
    char path[luapaths::MAX_PATH];
    for (HalFile entry = dir.openNextFile(); entry && apps.size() < MAX_APPS; entry = dir.openNextFile()) {
      if (!entry.isDirectory()) continue;
      if (entry.getName(name, sizeof(name)) == 0 || !appEntryPath(name, path, sizeof(path))) continue;
      AppEntry found;
      found.name = name;
      found.path = path;
      apps.push_back(std::move(found));
    }
  }

  // Metadata comes from the header of each script.
  auto header = makeUniqueNoThrow<char[]>(luapaths::HEADER_BYTES + 1);
  for (auto& meta : apps) {
    meta.title = meta.name;
    if (!header) continue;
    const size_t got =
        Storage.readFileToBuffer(meta.path.c_str(), header.get(), luapaths::HEADER_BYTES + 1, luapaths::HEADER_BYTES);
    std::string_view text(header.get(), got);
    while (!text.empty()) {
      const size_t eol = text.find('\n');
      const std::string_view line = text.substr(0, eol);
      if (const auto title = luapaths::headerValue(line, "title"); !title.empty()) meta.title.assign(title);
      if (const auto desc = luapaths::headerValue(line, "description"); !desc.empty()) meta.description.assign(desc);
      if (eol == std::string_view::npos) break;
      text.remove_prefix(eol + 1);
    }
  }
  if (!header) LOG_ERR("LAPP", "OOM: app header buffer");

  std::sort(apps.begin(), apps.end(),
            [](const AppEntry& a, const AppEntry& b) { return strcasecmp(a.title.c_str(), b.title.c_str()) < 0; });
}

int LuaAppListActivity::listCount() const { return static_cast<int>(rowItems.size()); }

const char* LuaAppListActivity::headerTitle() const { return tr(STR_LUA_APPS); }

void LuaAppListActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(apps.size())) return;
  auto activity = makeUniqueNoThrow<LuaAppActivity>(renderer, mappedInput, apps[index].path, apps[index].name);
  if (!activity) {
    LOG_ERR("LAPP", "OOM: app activity");
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(activity), [](const ActivityResult&) {});
}

void LuaAppListActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rowItems.empty()) {
    screen.centeredText(tr(STR_NO_LUA_APPS), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props);
  screen.list(props);
}
