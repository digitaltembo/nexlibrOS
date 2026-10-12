#include "LuaDict.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <lua.hpp>
#include <new>
#include <string>

#include "LuaRuntime.h"
#include "util/Dictionary.h"
#include "util/HtmlToPlainText.h"

namespace {
constexpr const char* WORK_METATABLE = "crosspoint.dict";
constexpr size_t MAX_WORD_BYTES = 128;
// Longest definition handed to Lua: about what gfx.wrap's 256 lines hold, and
// a fraction of the app heap. Raw text is cut at twice that before the plain
// text conversion so its copy stays bounded too.
constexpr size_t MAX_LUA_DEFINITION = 16 * 1024;
constexpr size_t MAX_RAW_DEFINITION = 2 * MAX_LUA_DEFINITION;
// Headroom kept free beyond the plain text copy, which aborts on OOM.
constexpr size_t CONVERT_HEADROOM_BYTES = 4 * 1024;

// Native lookup state lives in a Lua userdata so the GC destroys it if a Lua
// call raises while it is alive (see the binding rule in LuaRuntime.h).
struct DictWork {
  Dictionary dict;
  std::string definition;
  std::string headword;
};

int workGc(lua_State* L) {
  static_cast<DictWork*>(luaL_checkudata(L, 1, WORK_METATABLE))->~DictWork();
  return 0;
}

DictWork* newWork(lua_State* L) {
  auto* work = new (lua_newuserdatauv(L, sizeof(DictWork), 0)) DictWork();
  luaL_setmetatable(L, WORK_METATABLE);
  return work;
}

void indexYield(void*) { vTaskDelay(1); }

// Truncates text to at most maxBytes without splitting a UTF-8 sequence.
void truncateUtf8(std::string& text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return;
  size_t cut = maxBytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) cut--;
  text.resize(cut);
}

// Native half of dict.lookup; makes no Lua calls. Returns nullptr on a hit,
// with work.definition (plain text) and work.headword filled, else the error.
const char* lookupInto(DictWork& work, const char* folder, const char* word) {
  if (!work.dict.openFolder(folder)) return "not a dictionary";
  if (work.dict.needsIndex()) {
    Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
    if (!work.dict.buildIndex(&indexYield, nullptr, &indexResult)) {
      return indexResult == Dictionary::IndexResult::LowMemory ? "not enough memory" : "index failed";
    }
  }
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (!work.dict.lookup(word, work.definition, work.headword, &result)) {
    switch (result) {
      case Dictionary::LookupResult::LowMemory:
        return "not enough memory";
      case Dictionary::LookupResult::Decompress:
        return "decompress failed";
      case Dictionary::LookupResult::ReadError:
        return "read failed";
      default:
        return "not found";
    }
  }
  // StarDict multi-type entries separate their parts with NULs.
  std::replace(work.definition.begin(), work.definition.end(), '\0', '\n');
  truncateUtf8(work.definition, MAX_RAW_DEFINITION);
  if (ESP.getMaxAllocHeap() < work.definition.size() + CONVERT_HEADROOM_BYTES) {
    std::string().swap(work.definition);
    return "not enough memory";
  }
  work.definition = htmlToPlainText(work.definition);
  truncateUtf8(work.definition, MAX_LUA_DEFINITION);
  return nullptr;
}

// dict.status(folder) -> "ready" | "needs_index" | nil, err
// "needs_index": the next lookup first builds the index sidecars (seconds).
int dictStatus(lua_State* L) {
  char folder[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, folder, sizeof(folder));
  DictWork* work = newWork(L);
  const bool opened = work->dict.openFolder(folder);
  const bool needsIndex = opened && work->dict.needsIndex();
  if (!opened) {
    lua_pushnil(L);
    lua_pushliteral(L, "not a dictionary");
    return 2;
  }
  lua_pushstring(L, needsIndex ? "needs_index" : "ready");
  return 1;
}

// dict.lookup(folder, word) -> definition, headword | nil, err
int dictLookup(lua_State* L) {
  char folder[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, folder, sizeof(folder));
  size_t wordLen = 0;
  const char* word = luaL_checklstring(L, 2, &wordLen);
  if (wordLen == 0 || wordLen > MAX_WORD_BYTES) luaL_argerror(L, 2, "must be 1 to 128 bytes");
  DictWork* work = newWork(L);
  const char* error = lookupInto(*work, folder, word);
  if (error) {
    lua_pushnil(L);
    lua_pushstring(L, error);
    return 2;
  }
  lua_pushlstring(L, work->definition.data(), work->definition.size());
  lua_pushlstring(L, work->headword.data(), work->headword.size());
  // Hand the native buffers back now rather than at the next GC cycle.
  std::string().swap(work->definition);
  std::string().swap(work->headword);
  return 2;
}

constexpr luaL_Reg DICT_FUNCS[] = {{"status", dictStatus}, {"lookup", dictLookup}, {nullptr, nullptr}};
}  // namespace

void registerDictModule(lua_State* L) {
  luaL_newmetatable(L, WORK_METATABLE);
  lua_pushcfunction(L, workGc);
  lua_setfield(L, -2, "__gc");
  lua_pop(L, 1);
  luaL_newlib(L, DICT_FUNCS);
  lua_setglobal(L, "dict");
}
