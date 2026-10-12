-- title: Converter
-- description: Units, historical money and fictional coins
--
-- Data: Ye Olde Converter's units.slim.json and currencies.slim.json, in this
-- app's directory (/apps/converter). On first launch catalog.lua streams both
-- files into catalog.txt (names, ids and each record's byte offset) and
-- coins.txt (each currency's coins), rebuilt when either file changes size.
-- Then ui.lua runs the app, loading its other modules on demand.
--
-- This file only builds the catalog when needed, then loads the UI. The
-- builder is released before the UI loads, so the two never share the Lua
-- heap.

local UNITS = "units.slim.json"
local MONEY = "currencies.slim.json"
local CATALOG = "catalog.txt"
local COINS = "coins.txt"
local CATALOG_VERSION = "converter-catalog 3"
local BUILDER = "catalog.lua"

local status = "Loading... The first launch also builds a catalog of units and currencies, which takes longer."

local function catalog_stamp()
  local units_size, money_size = fs.size(UNITS), fs.size(MONEY)
  if not units_size or not money_size then
    error("Copy units.slim.json and currencies.slim.json from Ye Olde Converter's data folder to "
      .. sys.app_dir(), 0)
  end
  return CATALOG_VERSION .. "|" .. units_size .. "|" .. money_size
end

local function catalog_current(stamp)
  if not fs.size(COINS) then return false end
  local head = fs.read(CATALOG, 0, 64)
  return head ~= nil and (head:match("^[^\n]*"):gsub("%s+$", "")) == stamp
end

-- load() rather than require(): require caches the module, and the builder
-- is needed only this once.
local function build_catalog(stamp)
  local source = fs.read(BUILDER)
  if not source then error("Missing " .. BUILDER, 0) end
  local build = assert(load(source, "@converter/catalog.lua"))()
  source = nil
  build(UNITS, MONEY, CATALOG, COINS, stamp)
end

local function start()
  local stamp = catalog_stamp()
  if not catalog_current(stamp) then
    build_catalog(stamp)
    collectgarbage()
  end
  -- Replaces this file's callbacks, releasing its code.
  require("ui")(stamp)
end

function draw()
  gfx.font("ui_large", "bold")
  gfx.text(16, 16, "Converter")
  gfx.font("ui")
  local y = 64
  for _, line in ipairs(gfx.wrap(status, gfx.width() - 32, 10)) do
    gfx.text(16, y, line)
    y = y + gfx.line_height()
  end
  gfx.hints("Exit", nil, nil, nil)
end

-- Started on the first tick so the loading message is drawn first.
function on_tick()
  sys.set_tick(0)
  local ok, err = pcall(start)
  if not ok then status = tostring(err) end
  sys.redraw()
end

sys.set_tick(250)
