#include "LuaRuntime.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <InflateStream.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lua.hpp>

#include "LuaDict.h"

namespace {
// Instructions between stop-flag checks; ~1ms of bytecode at 160MHz.
constexpr int HOOK_INSTRUCTIONS = 1000;
// Lua runs at the loop task's priority; yield this often so the idle task
// (and its watchdog) still runs during long computations.
constexpr unsigned long YIELD_INTERVAL_MS = 50;
// fs.list result scratch: NUL-separated names, capped in bytes and entries.
constexpr size_t LIST_BUFFER_BYTES = 4096;
constexpr int LIST_MAX_ENTRIES = 256;
// fs.inflate: compressed bytes read from the card per fill, and the most it returns.
constexpr size_t INFLATE_INPUT_BYTES = 2048;
constexpr size_t INFLATE_MAX_OUTPUT = 16 * 1024;
// Decompressor state plus 32KB window, refused up front unless this much is contiguous.
constexpr size_t INFLATE_HEAP_BYTES = 44 * 1024;

// True when any '/'-separated segment of path is "." or "..".
bool hasDotSegment(const char* path, const size_t len) {
  size_t start = 0;
  for (size_t i = 0; i <= len; i++) {
    if (i < len && path[i] != '/') continue;
    const size_t segLen = i - start;
    if ((segLen == 1 && path[start] == '.') || (segLen == 2 && path[start] == '.' && path[start + 1] == '.')) {
      return true;
    }
    start = i + 1;
  }
  return false;
}

// Rewrites an absolute path in place into the form SdFat resolves: it skips
// separators and spaces before each component and trims trailing dots and
// spaces from it, so "/a//b. " names "/a/b". Access checks compare canonical
// paths so such aliases cannot slip past them. False when a component trims
// to nothing (e.g. "..." or " .").
bool canonicalize(char* path) {
  size_t r = 0;
  size_t w = 0;
  for (;;) {
    while (path[r] == '/' || path[r] == ' ') r++;
    if (path[r] == '\0') break;
    const size_t start = r;
    while (path[r] != '\0' && path[r] != '/') r++;
    size_t end = r;
    while (end > start && (path[end - 1] == '.' || path[end - 1] == ' ')) end--;
    if (end == start) return false;
    path[w++] = '/';
    memmove(path + w, path + start, end - start);
    w += end - start;
  }
  if (w == 0) path[w++] = '/';
  path[w] = '\0';
  return true;
}

// True when canonical path is dir or lies under it. Case-insensitive, as FAT is.
bool isInside(const char* path, const char* dir) {
  const size_t n = strlen(dir);
  if (n == 1 && dir[0] == '/') return path[0] == '/';
  return strncasecmp(path, dir, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

// True when canonical path is the entry script of appDir.
bool isEntryScript(const char* path, const char* appDir) {
  const size_t n = strlen(appDir);
  return strncasecmp(path, appDir, n) == 0 && path[n] == '/' && strcasecmp(path + n + 1, luapaths::ENTRY_FILE) == 0;
}

int pushFailure(lua_State* L, const char* what, const char* path) {
  lua_pushnil(L);
  lua_pushfstring(L, "%s: %s", what, path);
  return 2;
}

// fs.read(path [, offset [, length]]) -> string | nil, err
// Without offset the whole file is returned and must fit MAX_FILE_BYTES. With
// offset, returns up to length bytes (default and cap MAX_FILE_BYTES) from
// there; "" at or past the end.
int fsRead(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path));
  const bool ranged = !lua_isnoneornil(L, 2);
  const lua_Integer offsetArg = luaL_optinteger(L, 2, 0);
  const lua_Integer lengthArg = luaL_optinteger(L, 3, static_cast<lua_Integer>(LuaRuntime::MAX_FILE_BYTES));
  if (offsetArg < 0) luaL_argerror(L, 2, "must be >= 0");
  if (lengthArg < 0) luaL_argerror(L, 3, "must be >= 0");
  const auto offset = static_cast<size_t>(offsetArg);
  size_t size = 0;
  bool opened;
  {
    HalFile file;
    opened = Storage.openFileForRead("LUA", path, file);
    if (opened) size = file.fileSize();
  }
  if (!opened) return pushFailure(L, "cannot open", path);
  size_t count;
  if (ranged) {
    const size_t remaining = offset < size ? size - offset : 0;
    count = std::min({remaining, static_cast<size_t>(lengthArg), LuaRuntime::MAX_FILE_BYTES});
  } else {
    if (size > LuaRuntime::MAX_FILE_BYTES) return pushFailure(L, "file too large", path);
    count = size;
  }
  if (count == 0) {
    lua_pushliteral(L, "");
    return 1;
  }
  // Userdata scratch: reclaimed by the GC even if pushlstring raises OOM.
  auto* buf = static_cast<char*>(lua_newuserdatauv(L, count, 0));
  int got = -1;
  {
    HalFile file;
    if (Storage.openFileForRead("LUA", path, file) && file.seekSet(offset)) got = file.read(buf, count);
  }
  if (got != static_cast<int>(count)) return pushFailure(L, "read failed", path);
  lua_pushlstring(L, buf, count);
  return 1;
}

struct InflateInput {
  HalFile* file;
  uint8_t* buf;
  size_t remaining;
};

size_t fillInflateInput(void* ctx, const uint8_t** data) {
  auto* in = static_cast<InflateInput*>(ctx);
  const size_t want = std::min(in->remaining, INFLATE_INPUT_BYTES);
  if (want == 0) return 0;
  const int got = in->file->read(in->buf, want);
  if (got <= 0) return 0;
  in->remaining -= static_cast<size_t>(got);
  *data = in->buf;
  return static_cast<size_t>(got);
}

// fs.inflate(path, offset, clen, skip, len) -> string | nil, err
// Decompresses the raw deflate stream stored at [offset, offset+clen) and
// returns its output bytes [skip, skip+len), shorter if the stream ends first.
// len is capped at 16KB; skip may be any size, as output streams through a
// 32KB window and only the requested slice is kept.
int fsInflate(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path));
  const lua_Integer offset = luaL_checkinteger(L, 2);
  const lua_Integer clen = luaL_checkinteger(L, 3);
  const lua_Integer skipArg = luaL_checkinteger(L, 4);
  const lua_Integer lenArg = luaL_checkinteger(L, 5);
  if (offset < 0) luaL_argerror(L, 2, "must be >= 0");
  if (clen <= 0) luaL_argerror(L, 3, "must be > 0");
  if (skipArg < 0) luaL_argerror(L, 4, "must be >= 0");
  if (lenArg < 0) luaL_argerror(L, 5, "must be >= 0");
  const size_t len = std::min(static_cast<size_t>(lenArg), INFLATE_MAX_OUTPUT);
  if (len == 0) {
    lua_pushliteral(L, "");
    return 1;
  }
  // Lua allocations come first: an OOM raised here unwinds past no C++ objects.
  auto* out = static_cast<uint8_t*>(lua_newuserdatauv(L, len, 0));
  if (ESP.getMaxAllocHeap() < INFLATE_HEAP_BYTES) return pushFailure(L, "not enough memory", path);

  const char* error = nullptr;
  size_t produced = 0;
  {
    HalFile file;
    auto input = makeUniqueNoThrow<uint8_t[]>(INFLATE_INPUT_BYTES);
    InflateStream stream;
    if (!input || !stream.init(true)) {
      error = "not enough memory";
    } else if (!Storage.openFileForRead("LUA", path, file) || !file.seekSet(static_cast<size_t>(offset))) {
      error = "cannot open";
    } else {
      InflateInput in{&file, input.get(), static_cast<size_t>(clen)};
      stream.setFill(fillInflateInput, &in);
      size_t skip = static_cast<size_t>(skipArg);
      InflateStream::Status status = InflateStream::Status::Ok;
      // Skipped output lands in the result buffer and is overwritten.
      while (skip > 0 && status == InflateStream::Status::Ok) {
        size_t n = 0;
        status = stream.readAtMost(out, std::min(skip, len), &n);
        skip -= n;
      }
      if (status == InflateStream::Status::Ok) status = stream.readAtMost(out, len, &produced);
      if (status == InflateStream::Status::Error) error = "decompress failed";
    }
  }
  if (error) return pushFailure(L, error, path);
  lua_pushlstring(L, reinterpret_cast<const char*>(out), produced);
  return 1;
}

// fs.size(path) -> bytes | nil, err
int fsSize(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path));
  size_t size = 0;
  bool opened;
  {
    HalFile file;
    opened = Storage.openFileForRead("LUA", path, file);
    if (opened) size = file.fileSize();
  }
  if (!opened) return pushFailure(L, "cannot open", path);
  lua_pushinteger(L, static_cast<lua_Integer>(size));
  return 1;
}

int writeImpl(lua_State* L, const bool append) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path), true);
  size_t len = 0;
  const char* data = luaL_checklstring(L, 2, &len);
  const bool positioned = !append && !lua_isnoneornil(L, 3);
  const lua_Integer offsetArg = positioned ? luaL_checkinteger(L, 3) : 0;
  if (offsetArg < 0) luaL_argerror(L, 3, "must be >= 0");
  const auto offset = static_cast<size_t>(offsetArg);
  bool ok = false;
  bool pastEnd = false;
  {
    HalFile file;
    if (append) {
      file = Storage.open(path, O_WRONLY | O_CREAT | O_APPEND);
      ok = static_cast<bool>(file);
    } else if (positioned) {
      file = Storage.open(path, O_RDWR | O_CREAT);
      ok = static_cast<bool>(file);
      pastEnd = ok && offset > file.fileSize();
      ok = ok && !pastEnd && file.seekSet(offset);
    } else {
      ok = Storage.openFileForWrite("LUA", path, file);
    }
    if (ok) ok = file.write(data, len) == len;
  }
  if (pastEnd) return pushFailure(L, "offset past end", path);
  if (!ok) return pushFailure(L, "write failed", path);
  lua_pushboolean(L, 1);
  return 1;
}

// fs.write(path, data [, offset]) -> true | nil, err. Without offset the file
// is replaced; with one, data overwrites in place (offset <= current size).
int fsWrite(lua_State* L) { return writeImpl(L, false); }
// fs.append(path, data) -> true | nil, err
int fsAppend(lua_State* L) { return writeImpl(L, true); }

// fs.exists(path) -> bool
int fsExists(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path));
  lua_pushboolean(L, Storage.exists(path));
  return 1;
}

// fs.remove(path) -> bool (files only)
int fsRemove(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path), true);
  lua_pushboolean(L, Storage.remove(path));
  return 1;
}

// fs.mkdir(path) -> bool (creates parents)
int fsMkdir(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path), true);
  lua_pushboolean(L, Storage.mkdir(path));
  return 1;
}

// fs.list(path) -> { "file.txt", "subdir/", ... } | nil, err
// Unsorted directory order; at most LIST_MAX_ENTRIES names.
int fsList(lua_State* L) {
  char path[luapaths::MAX_PATH];
  LuaRuntime::checkPath(L, 1, path, sizeof(path));
  auto* buf = static_cast<char*>(lua_newuserdatauv(L, LIST_BUFFER_BYTES, 0));
  size_t used = 0;
  int count = 0;
  bool opened = false;
  {
    HalFile dir = Storage.open(path);
    opened = dir && dir.isDirectory();
    if (opened) {
      for (HalFile entry = dir.openNextFile(); entry && count < LIST_MAX_ENTRIES; entry = dir.openNextFile()) {
        // Room for the name, a '/' marker, and the separator.
        const size_t room = LIST_BUFFER_BYTES - used;
        if (room < 3) break;
        const size_t nameLen = entry.getName(buf + used, room - 1);
        if (nameLen == 0 || nameLen + 2 > room) continue;
        used += nameLen;
        if (entry.isDirectory()) buf[used++] = '/';
        buf[used++] = '\0';
        count++;
      }
    }
  }
  if (!opened) return pushFailure(L, "not a directory", path);
  lua_createtable(L, count, 0);
  size_t offset = 0;
  for (int i = 1; i <= count; i++) {
    const size_t len = strlen(buf + offset);
    lua_pushlstring(L, buf + offset, len);
    lua_rawseti(L, -2, i);
    offset += len + 1;
  }
  return 1;
}

constexpr luaL_Reg FS_FUNCS[] = {{"read", fsRead},       {"write", fsWrite}, {"append", fsAppend}, {"exists", fsExists},
                                 {"remove", fsRemove},   {"mkdir", fsMkdir}, {"list", fsList},     {"size", fsSize},
                                 {"inflate", fsInflate}, {nullptr, nullptr}};

// print(...) -> serial log at INFO.
int luaPrint(lua_State* L) {
  const int n = lua_gettop(L);
  for (int i = 1; i <= n; i++) {
    if (i > 1) lua_pushliteral(L, "\t");
    luaL_tolstring(L, i, nullptr);
  }
  if (n == 0) lua_pushliteral(L, "");
  lua_concat(L, n == 0 ? 1 : 2 * n - 1);
  LOG_INF("LUA", "%s", lua_tostring(L, -1));
  return 0;
}

// load(chunk [, chunkname [, mode [, env]]]): string chunks only, always text
// mode — precompiled bytecode can crash the VM.
int luaLoadText(lua_State* L) {
  size_t len = 0;
  const char* source = luaL_checklstring(L, 1, &len);
  const char* chunkName = luaL_optstring(L, 2, "=(load)");
  const bool hasEnv = !lua_isnone(L, 4);
  if (luaL_loadbufferx(L, source, len, chunkName, "t") != LUA_OK) {
    lua_pushnil(L);
    lua_insert(L, -2);
    return 2;
  }
  if (hasEnv) {
    lua_pushvalue(L, 4);
    if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
  }
  return 1;
}

// require("name.sub") -> <app dir>/name/sub.lua, else LIB_DIR/name/sub.lua;
// cached in package.loaded style.
int luaRequire(lua_State* L) {
  size_t len = 0;
  const char* name = luaL_checklstring(L, 1, &len);
  if (len == 0 || len > 64 || name[0] == '.') luaL_argerror(L, 1, "invalid module name");
  for (size_t i = 0; i < len; i++) {
    const char c = name[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                    c == '-' || (c == '.' && name[i + 1] != '.' && name[i + 1] != '\0');
    if (!ok) luaL_argerror(L, 1, "invalid module name");
  }
  lua_settop(L, 1);
  lua_getfield(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);  // 2: loaded
  lua_getfield(L, 2, name);
  if (lua_toboolean(L, -1)) return 1;
  lua_pop(L, 1);

  // "@" prefix makes the same buffer the chunk name, so errors cite the file.
  char chunk[luapaths::MAX_PATH];
  const char* const dirs[] = {LuaRuntime::from(L)->appDir(), luapaths::LIB_DIR};
  bool found = false;
  for (const char* dir : dirs) {
    const int written = snprintf(chunk, sizeof(chunk), "@%s/%s.lua", dir, name);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(chunk)) luaL_argerror(L, 1, "module name too long");
    char* const modulePart = chunk + 1 + strlen(dir) + 1;
    for (char* c = modulePart; *c != '\0'; c++) {
      if (*c == '.' && strcmp(c, ".lua") != 0) *c = '/';
    }
    found = Storage.exists(chunk + 1);
    if (found) break;
  }
  if (!found) luaL_error(L, "module '%s' not found in the app directory or %s", name, luapaths::LIB_DIR);
  LuaRuntime::loadFile(L, chunk + 1, chunk);
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, 2, name);
  return 1;
}

int luaPanic(lua_State* L) {
  LOG_ERR("LUA", "Unprotected error: %s", lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "?");
  return 0;  // Lua aborts after the panic handler returns
}
}  // namespace

std::string_view luapaths::headerValue(std::string_view line, const std::string_view key) {
  if (line.substr(0, 2) != "--") return {};
  line.remove_prefix(2);
  while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
  if (line.substr(0, key.size()) != key || line.substr(key.size(), 1) != ":") return {};
  line.remove_prefix(key.size() + 1);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t')) line.remove_suffix(1);
  return line;
}

LuaRuntime::~LuaRuntime() { close(); }

void LuaRuntime::checkPath(lua_State* L, const int idx, char* out, const size_t outLen, const bool write) {
  size_t len = 0;
  const char* p = luaL_checklstring(L, idx, &len);
  if (len == 0 || memchr(p, '\0', len) != nullptr) luaL_argerror(L, idx, "invalid path");
  if (hasDotSegment(p, len)) luaL_argerror(L, idx, "'.' and '..' are not allowed");
  const LuaRuntime* self = from(L);
  const char* appDir = self->appDir();
  int written;
  if (p[0] == '/') {
    written = snprintf(out, outLen, "%s", p);
  } else {
    written = snprintf(out, outLen, "%s/%s", appDir, p);
  }
  if (written < 0 || static_cast<size_t>(written) >= outLen) luaL_argerror(L, idx, "path too long");
  if (!canonicalize(out)) luaL_argerror(L, idx, "invalid path segment");
  if (isInside(out, appDir)) {
    if (write && isEntryScript(out, appDir)) luaL_argerror(L, idx, "access denied (apps cannot modify index.lua)");
  } else if (write) {
    if (isInside(out, luapaths::APPS_DIR) || !self->granted(out, true)) {
      luaL_argerror(L, idx, "access denied (no write permission)");
    }
  } else if (!isInside(out, luapaths::LIB_DIR) && !self->granted(out, false)) {
    luaL_argerror(L, idx, "access denied (no read permission)");
  }
}

bool LuaRuntime::granted(const char* path, const bool write) const {
  for (uint8_t i = 0; i < grantCount; i++) {
    if ((grants[i].write || !write) && isInside(path, grants[i].path)) return true;
  }
  return false;
}

bool LuaRuntime::addGrant(std::string_view token, char* err, const size_t errLen) {
  const std::string_view original = token;
  bool write;
  if (token.substr(0, 5) == "read:") {
    write = false;
    token.remove_prefix(5);
  } else if (token.substr(0, 6) == "write:") {
    write = true;
    token.remove_prefix(6);
  } else {
    snprintf(err, errLen, "unknown permission '%.*s'", static_cast<int>(original.size()), original.data());
    return false;
  }
  if (grantCount >= MAX_GRANTS) {
    snprintf(err, errLen, "more than %d permissions", static_cast<int>(MAX_GRANTS));
    return false;
  }
  Grant& grant = grants[grantCount];
  const bool valid = !token.empty() && token.front() == '/' && token.size() < MAX_GRANT_PATH &&
                     memchr(token.data(), '\0', token.size()) == nullptr && !hasDotSegment(token.data(), token.size());
  if (valid) {
    memcpy(grant.path, token.data(), token.size());
    grant.path[token.size()] = '\0';
  }
  if (!valid || !canonicalize(grant.path)) {
    snprintf(err, errLen, "invalid permission path '%.*s'", static_cast<int>(original.size()), original.data());
    return false;
  }
  // Apps never write under APPS_DIR beyond their own directory, so a write
  // grant there could only reach other apps' scripts.
  if (write && (isInside(grant.path, luapaths::APPS_DIR) || isInside(luapaths::APPS_DIR, grant.path))) {
    snprintf(err, errLen, "write permission may not cover %s", luapaths::APPS_DIR);
    return false;
  }
  grant.write = write;
  grantCount++;
  LOG_INF("LUA", "%s: %s access to %s", appDirPath, write ? "write" : "read", grant.path);
  return true;
}

bool LuaRuntime::loadPermissions(char* err, const size_t errLen) {
  grantCount = 0;
  char path[luapaths::MAX_PATH];
  snprintf(path, sizeof(path), "%s/%s", appDirPath, luapaths::ENTRY_FILE);
  auto header = makeUniqueNoThrow<char[]>(luapaths::HEADER_BYTES + 1);
  if (!header) {
    LOG_ERR("LUA", "OOM: permission header buffer");
    snprintf(err, errLen, "not enough memory");
    return false;
  }
  const size_t got = Storage.readFileToBuffer(path, header.get(), luapaths::HEADER_BYTES + 1, luapaths::HEADER_BYTES);
  std::string_view text(header.get(), got);
  while (!text.empty()) {
    const size_t eol = text.find('\n');
    // A line cut off by the header limit could carry a truncated path.
    if (eol == std::string_view::npos && got == luapaths::HEADER_BYTES) break;
    std::string_view value = luapaths::headerValue(text.substr(0, eol), "permissions");
    while (!value.empty()) {
      const size_t sep = value.find_first_of(" ,\t");
      const std::string_view token = value.substr(0, sep);
      if (!token.empty() && !addGrant(token, err, errLen)) return false;
      if (sep == std::string_view::npos) break;
      value.remove_prefix(sep + 1);
    }
    if (eol == std::string_view::npos) break;
    text.remove_prefix(eol + 1);
  }
  return true;
}

LuaRuntime* LuaRuntime::from(lua_State* L) { return *static_cast<LuaRuntime**>(lua_getextraspace(L)); }

void* LuaRuntime::alloc(void* ud, void* ptr, const size_t osize, const size_t nsize) {
  auto* self = static_cast<LuaRuntime*>(ud);
  // With ptr == nullptr, osize encodes the object type, not a size.
  const size_t oldSize = ptr ? osize : 0;
  if (nsize == 0) {
    free(ptr);
    self->used -= oldSize;
    return nullptr;
  }
  if (nsize > oldSize) {
    const size_t growth = nsize - oldSize;
    if (self->used + growth > HEAP_CAP) return nullptr;
    // Internal RAM only: with PSRAM, malloc still places blocks under
    // SPIRAM_MALLOC_ALWAYSINTERNAL (nearly all Lua objects) in internal RAM,
    // and the default free size would count the PSRAM too.
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < MIN_FREE_HEAP + growth) return nullptr;
  }
  void* block = realloc(ptr, nsize);
  if (!block) return nullptr;
  self->used = self->used - oldSize + nsize;
  return block;
}

void LuaRuntime::hook(lua_State* L, lua_Debug* /*ar*/) {
  LuaRuntime* self = from(L);
  if (self->stop && self->stop->load(std::memory_order_relaxed)) luaL_error(L, "app stopped");
  const unsigned long now = millis();
  if (now - self->lastYieldMs >= YIELD_INTERVAL_MS) {
    self->lastYieldMs = now;
    vTaskDelay(1);
  }
}

int LuaRuntime::openLibs(lua_State* L) {
  static constexpr luaL_Reg LIBS[] = {{LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine},
                                      {LUA_TABLIBNAME, luaopen_table},  {LUA_OSLIBNAME, luaopen_os},
                                      {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                      {LUA_UTF8LIBNAME, luaopen_utf8}};
  for (const auto& lib : LIBS) {
    luaL_requiref(L, lib.name, lib.func, 1);
    lua_pop(L, 1);
  }

  // Base functions that reach the host filesystem through stdio.
  static constexpr const char* REMOVED_BASE[] = {"dofile", "loadfile"};
  for (const char* fn : REMOVED_BASE) {
    lua_pushnil(L);
    lua_setglobal(L, fn);
  }
  lua_register(L, "print", luaPrint);
  lua_register(L, "load", luaLoadText);
  lua_register(L, "require", luaRequire);

  // os keeps time/date/clock/difftime only.
  static constexpr const char* REMOVED_OS[] = {"execute", "exit", "getenv", "remove", "rename", "tmpname", "setlocale"};
  lua_getglobal(L, LUA_OSLIBNAME);
  for (const char* fn : REMOVED_OS) {
    lua_pushnil(L);
    lua_setfield(L, -2, fn);
  }
  lua_pop(L, 1);

  luaL_newlib(L, FS_FUNCS);
  lua_setglobal(L, "fs");
  registerDictModule(L);
  return 0;
}

bool LuaRuntime::open(const char* appName, const std::atomic<bool>* stopFlag, void* host, char* err,
                      const size_t errLen) {
  close();
  stop = stopFlag;
  hostPtr = host;
  used = 0;
  snprintf(appDirPath, sizeof(appDirPath), "%s/%s", luapaths::APPS_DIR, appName);
  if (!loadPermissions(err, errLen)) return false;

  L = lua_newstate(&LuaRuntime::alloc, this);
  if (!L) {
    snprintf(err, errLen, "not enough memory");
    return false;
  }
  *static_cast<LuaRuntime**>(lua_getextraspace(L)) = this;
  lua_atpanic(L, luaPanic);
  lua_sethook(L, &LuaRuntime::hook, LUA_MASKCOUNT, HOOK_INSTRUCTIONS);
  // Collect more eagerly than the default pause (200%): heap is the
  // constraint here, not GC CPU time.
  lua_gc(L, LUA_GCINC, 120, 0, 0);

  if (!call(&LuaRuntime::openLibs, nullptr, err, errLen)) {
    close();
    return false;
  }
  return true;
}

void LuaRuntime::close() {
  if (!L) return;
  lua_close(L);
  L = nullptr;
  LOG_DBG("LUA", "State closed (%u bytes still tracked)", static_cast<unsigned>(used));
}

bool LuaRuntime::call(int (*fn)(lua_State*), void* ctx, char* err, const size_t errLen) {
  if (!L) {
    snprintf(err, errLen, "no Lua state");
    return false;
  }
  lastYieldMs = millis();
  lua_pushcfunction(L, fn);
  lua_pushlightuserdata(L, ctx);
  const int rc = lua_pcall(L, 1, 0, 0);
  if (rc == LUA_OK) return true;
  const char* msg = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "(error object is not a string)";
  snprintf(err, errLen, "%s", msg);
  lua_pop(L, 1);
  return false;
}

namespace {
constexpr size_t SCRIPT_CHUNK = 1024;

struct ScriptReader {
  const char* path;
  char* buf;  // SCRIPT_CHUNK bytes of Lua userdata
  size_t offset;
  size_t size;
  bool failed;
};

// lua_load reader: streams the script so its source never sits in RAM whole
// while it compiles. Each call opens, reads and closes the file, so no HalFile
// is alive while the parser (which can raise) runs.
const char* readScriptChunk(lua_State* /*L*/, void* data, size_t* len) {
  auto* reader = static_cast<ScriptReader*>(data);
  *len = 0;
  if (reader->offset >= reader->size) return nullptr;
  const size_t want = std::min(SCRIPT_CHUNK, reader->size - reader->offset);
  int got = -1;
  {
    HalFile file;
    if (Storage.openFileForRead("LUA", reader->path, file) && file.seekSet(reader->offset)) {
      got = file.read(reader->buf, want);
    }
  }
  if (got <= 0) {
    reader->failed = true;
    return nullptr;
  }
  reader->offset += static_cast<size_t>(got);
  *len = static_cast<size_t>(got);
  return reader->buf;
}
}  // namespace

void LuaRuntime::loadFile(lua_State* L, const char* path, const char* chunkName) {
  size_t size = 0;
  bool opened;
  {
    HalFile file;
    opened = Storage.openFileForRead("LUA", path, file);
    if (opened) size = file.fileSize();
  }
  if (!opened) luaL_error(L, "cannot open %s", path);
  if (size > MAX_FILE_BYTES) luaL_error(L, "%s is larger than %d bytes", path, static_cast<int>(MAX_FILE_BYTES));
  ScriptReader reader{path, static_cast<char*>(lua_newuserdatauv(L, SCRIPT_CHUNK, 0)), 0, size, false};
  const int rc = lua_load(L, &readScriptChunk, &reader, chunkName, "t");
  if (reader.failed) luaL_error(L, "cannot read %s", path);
  if (rc != LUA_OK) lua_error(L);
  lua_remove(L, -2);  // read buffer; the compiled chunk no longer needs it
}
