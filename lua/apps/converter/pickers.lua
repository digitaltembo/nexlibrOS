-- Converter pickers, loaded by ui.lua's C.use() to open a picker. The picker's
-- items hold this chunk's closures, so its code stays loaded while S.picker is
-- open and is released with it. Chunk argument: ui.lua's shared table C.
-- Returns { open = open(target) }.
--
-- Choosing a row sets S.from or S.to and S.after_pick; ui.lua converts once
-- this code is no longer reachable, so the two are never loaded together.

local C = ...
local S = C.S
local year_text = C.year_text

local CURRENCY_ROW = "(%d+)|([^|\n]*)|([^|\n]*)|([^|\n]*)|([^|\n]*)|(%d+)|(%d+)|(%a)"

local function sections(packed)
  local out = {}
  for off, len, count, priced, name in packed:gmatch("(%d+)|(%d+)|(%d+)|(%d+)|([^\n]*)") do
    out[#out + 1] = { offset = tonumber(off), length = tonumber(len), count = tonumber(count),
      priced = tonumber(priced), name = name }
  end
  return out
end

local function section_rows(section)
  return fs.read(C.CATALOG, section.offset, section.length) or ""
end

local function unit_items(section)
  local out = {}
  for off, id, name in section_rows(section):gmatch("(%d+)|([^|\n]*)|([^\n]*)") do
    out[#out + 1] = { offset = tonumber(off), id = id, name = name }
  end
  return out
end

local function currency_item(off, id, name, first, last, coins_off, coins_len, kind)
  return { offset = tonumber(off), id = id, name = name, first = tonumber(first), last = tonumber(last),
    coins_offset = tonumber(coins_off), coins_length = tonumber(coins_len), kind = kind }
end

local function currency_items(section)
  local out = {}
  for off, id, name, first, last, coins_off, coins_len, kind in section_rows(section):gmatch(CURRENCY_ROW) do
    out[#out + 1] = currency_item(off, id, name, first, last, coins_off, coins_len, kind)
  end
  return out
end

local function push(title, items)
  S.picker.stack[#S.picker.stack + 1] = { title = title, items = items, view = C.listview.new(56) }
end

local function finish(sel)
  local target = S.picker.target
  S.picker = nil
  if target == "from" then
    S.from = sel
    if S.to and not C.compatible(S.from, S.to) then S.to = nil end
  else
    S.to = sel
  end
  S.after_pick = true
end

local function push_units(kind, system)
  local items = {}
  for _, u in ipairs(unit_items(system)) do
    items[#items + 1] = { label = u.name, pick = function()
      finish({ kind = "unit", category = kind.key, name = u.name, system = system.name, offset = u.offset })
    end }
  end
  push(kind.name .. ": " .. system.name, items)
end

local function push_systems(kind)
  local items = {}
  for _, s in ipairs(sections(kind.systems)) do
    items[#items + 1] = { label = s.name, sub = tostring(s.count), pick = function() push_units(kind, s) end }
  end
  push(kind.name, items)
end

-- Builds the item list with coins.lua loaded, which is released on return.
local function denomination_items(c, year)
  local coins = C.use("coins.lua")
  local d = coins.read(c)
  local base = coins.base_denomination(d)
  local list = {}
  for _, den in ipairs(d.denoms) do
    if coins.in_use(d, den, year) then list[#list + 1] = den end
  end
  if #list == 0 then -- outside every coin's dates: offer them all
    for _, den in ipairs(d.denoms) do list[#list + 1] = den end
  end
  local has_base = false
  for _, den in ipairs(list) do has_base = has_base or den == base end
  if not has_base then list[#list + 1] = base end
  local values = {}
  for _, den in ipairs(list) do values[den] = coins.log10(coins.denom_value(den, year)) end
  table.sort(list, function(a, b) return values[a] > values[b] end)
  local items = {}
  for _, den in ipairs(list) do
    local label = den.name .. (den.symbol and (" (" .. den.symbol .. ")") or "")
    local sub = den == base and "base unit"
        or ("= " .. coins.fmt(coins.denom_value(den, year), 4) .. " " .. tostring(d.base_unit))
    items[#items + 1] = { label = label, sub = sub, den = den }
  end
  return items
end

local function push_denominations(c, year)
  local ok, items = pcall(denomination_items, c, year)
  if not ok then
    S.message = tostring(items)
    return
  end
  for _, item in ipairs(items) do
    local den = item.den
    item.den = nil
    item.pick = function() finish({ kind = "money", cur = c, year = year, denom = den }) end
  end
  push(c.name .. (year and (", " .. year_text(year)) or ""), items)
end

local function pick_currency(c)
  if c.kind ~= "r" then return push_denominations(c, nil) end
  local default = S.from and S.from.kind == "money" and S.from.year or c.last
  default = math.max(c.first, math.min(c.last, default))
  local range = year_text(c.first) .. " to " .. year_text(c.last)
  C.numpad({
    title = "Year (" .. range .. ")",
    text = tostring(default),
    max_len = 12,
    keys = "INTEGER",
    note = c.first < 0 and "± for BC" or nil,
    check = function(text)
      local y = C.parse_year(text)
      if not y then return "Not a year: " .. text end
      if y < c.first or y > c.last then return "No data for " .. year_text(y) .. " (" .. range .. ")" end
    end,
    done = function(text)
      if text then push_denominations(c, C.parse_year(text)) end
    end,
  })
end

local function currency_sub(c)
  if c.kind == "r" then return year_text(c.first) .. " to " .. year_text(c.last) end
  return c.kind == "f" and "fictional" or "fictional, no $ value"
end

local function push_currencies(group, priced_only)
  local items = {}
  for _, c in ipairs(currency_items(group)) do
    if not priced_only or c.kind ~= "n" then
      items[#items + 1] = { label = c.name, sub = currency_sub(c), pick = function() pick_currency(c) end }
    end
  end
  push(group.name, items)
end

local function usd_base(usd)
  local coins = C.use("coins.lua")
  return coins.base_denomination(coins.read(usd))
end

local function push_groups(priced_only)
  local items = {}
  local usd = priced_only and S.catalog.usd and currency_item(S.catalog.usd:match(CURRENCY_ROW))
  if usd then
    items[#items + 1] = { label = "US dollar, " .. usd.last, sub = "today", pick = function()
      local ok, den = pcall(usd_base, usd)
      if not ok then
        S.message = tostring(den)
        return
      end
      finish({ kind = "money", cur = usd, year = usd.last, denom = den })
    end }
  end
  for _, g in ipairs(sections(S.catalog.groups)) do
    local n = priced_only and g.priced or g.count
    if n > 0 then
      items[#items + 1] = { label = g.name, sub = tostring(n), pick = function() push_currencies(g, priced_only) end }
    end
  end
  push("Money", items)
end

local function push_kinds()
  local items = {}
  for _, k in ipairs(S.catalog.kinds) do
    items[#items + 1] = { label = k.name, sub = k.count .. " units", pick = function() push_systems(k) end }
  end
  items[#items + 1] = { label = "Money", sub = S.catalog.money_count .. " currencies", pick = function()
    push_groups(false)
  end }
  push("What is it?", items)
end

local function open(target)
  S.picker = { target = target, stack = {} }
  local from = S.from
  if target == "to" and from then
    if from.kind == "unit" then
      for _, k in ipairs(S.catalog.kinds) do
        if k.key == from.category then push_systems(k) end
      end
    elseif from.cur.kind == "n" then
      push_denominations(from.cur, nil)
    else
      push_groups(true)
    end
  else
    push_kinds()
  end
  if #S.picker.stack == 0 then S.picker = nil end
end

return { open = open }
