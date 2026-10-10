#pragma once

struct lua_State;

// Registers the `dict` global: StarDict lookups over dictionary folders the
// app may read (see LuaRuntime::checkPath). Call from a protected function.
void registerDictModule(lua_State* L);
