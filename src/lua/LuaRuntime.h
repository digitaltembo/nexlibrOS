#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

struct lua_State;

// SD locations for Lua apps. Each directory APPS_DIR/<name>/ holding
// ENTRY_FILE is one app. An app reads and writes its own directory (relative
// fs paths resolve there), reads LIB_DIR, the shared libraries, and reaches
// whatever its permission header grants (see LuaRuntime).
// require("a.b") loads <app dir>/a/b.lua, else LIB_DIR/a/b.lua.
namespace luapaths {
inline constexpr const char* APPS_DIR = "/apps";
inline constexpr const char* LIB_DIR = "/apps/.lib";
inline constexpr const char* ENTRY_FILE = "index.lua";
inline constexpr size_t MAX_PATH = 192;
// "-- key: value" metadata comments are read from this many leading bytes of
// ENTRY_FILE.
inline constexpr size_t HEADER_BYTES = 512;

// Returns the trimmed value of a "-- key: value" comment line, or "".
std::string_view headerValue(std::string_view line, std::string_view key);
}  // namespace luapaths

// Sandboxed Lua 5.4 state for SD apps: capped allocator, cooperative stop
// hook, restricted stdlib (no io/package/debug, no dofile/loadfile, text-only
// load, os without process/file access), require() over the app directory
// and LIB_DIR, and the `fs` and `dict` modules confined to those directories
// plus the directories the app's "-- permissions:" header grants. An app never
// writes its own ENTRY_FILE (where its permissions are declared) or anything
// else under APPS_DIR outside its directory.
//
// Every entry into Lua must go through call(), which runs a C function under
// lua_pcall: an unprotected error would reach the panic handler and abort().
//
// Binding rule: Lua errors longjmp and -fno-exceptions means C++ destructors
// between the error and the pcall are skipped. A binding must not hold
// HalFile, std::string, unique_ptr or other non-trivial objects across any
// Lua API call that can raise (luaL_check*, lua_push* of strings/tables,
// luaL_error). Scope C++ work in blocks that make no Lua calls, and use Lua
// userdata for scratch buffers so the GC reclaims them on error.
class LuaRuntime {
 public:
  // Lua heap ceiling per app; the allocator also refuses growth that would
  // leave less than MIN_FREE_HEAP for the rest of the firmware. Lua objects
  // are roughly 1.5x larger with 64-bit pointers (16-byte TValues), so the
  // native simulator gets a proportional cap to fit what fits on the device.
  static constexpr size_t HEAP_CAP = (sizeof(void*) > 4 ? 144 : 96) * 1024;
  static constexpr size_t MIN_FREE_HEAP = 40 * 1024;
  // Largest file fs.read / require / the app loader will pull into RAM.
  static constexpr size_t MAX_FILE_BYTES = 32 * 1024;

  LuaRuntime() = default;
  ~LuaRuntime();
  LuaRuntime(const LuaRuntime&) = delete;
  LuaRuntime& operator=(const LuaRuntime&) = delete;

  // appName names the app directory (APPS_DIR/<appName>); stopFlag aborts
  // running Lua at the next hook when set; host is returned by host() for
  // app-specific bindings.
  bool open(const char* appName, const std::atomic<bool>* stopFlag, void* host, char* err, size_t errLen);
  void close();
  // Forgets the state without closing it, for when its owning task was killed
  // mid-call and the state may be inconsistent. Leaks the Lua heap.
  void abandon() { L = nullptr; }
  lua_State* state() const { return L; }

  // Runs fn(L) under lua_pcall with ctx as its single light-userdata argument.
  // On failure copies the error message into err and returns false.
  bool call(int (*fn)(lua_State*), void* ctx, char* err, size_t errLen);

  // Loads the SD file at path as a text chunk and pushes the compiled function.
  // Raises a Lua error on failure; call from inside a protected function.
  static void loadFile(lua_State* L, const char* path, const char* chunkName);

  // Resolves Lua arg idx to an absolute SD path in out, in the form SdFat
  // resolves it (single separators, components with leading spaces and
  // trailing dots/spaces trimmed). Relative paths land under the app
  // directory. Reads may reach the app directory, LIB_DIR and granted
  // directories; writes the app directory (except ENTRY_FILE) and write
  // grants. Raises on "."/".." segments, overlong paths and any other location.
  static void checkPath(lua_State* L, int idx, char* out, size_t outLen, bool write = false);

  static LuaRuntime* from(lua_State* L);
  void* host() const { return hostPtr; }
  const char* appDir() const { return appDirPath; }
  size_t heapUsed() const { return used; }

  // Directories granted by "-- permissions:" lines in the entry script header.
  static constexpr size_t MAX_GRANTS = 8;
  static constexpr size_t MAX_GRANT_PATH = 64;
  struct Grant {
    char path[MAX_GRANT_PATH];
    bool write;  // write implies read
  };

 private:
  static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize);
  static void hook(lua_State* L, struct lua_Debug* ar);
  static int openLibs(lua_State* L);
  // Parses the entry script's permission header into grants.
  bool loadPermissions(char* err, size_t errLen);
  bool addGrant(std::string_view token, char* err, size_t errLen);
  bool granted(const char* path, bool write) const;

  lua_State* L = nullptr;
  const std::atomic<bool>* stop = nullptr;
  void* hostPtr = nullptr;
  size_t used = 0;
  unsigned long lastYieldMs = 0;
  char appDirPath[64] = {};
  Grant grants[MAX_GRANTS] = {};
  uint8_t grantCount = 0;
};
