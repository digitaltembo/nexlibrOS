-- Builds the Converter app's catalog.txt and coins.txt from Ye Olde
-- Converter's slim JSON. Loaded only when the catalog is missing or stale, and
-- released afterwards, so its code does not stay in the app's heap.
--
-- catalog.txt:
--   <stamp>
--   K|<kind key>|<kind name>|<base unit>
--   S|<system>                 then "<offset>|<id>|<name>" rows, smallest first
--   G|<region>                 then currency rows, oldest first:
--     <offset>|<id>|<name>|<first>|<last>|<coins offset>|<coins length>|<kind>
-- Offsets are byte positions of each record in the JSON. Currency kind r is
-- valued by year, f is fictional with one value, n has no dollar value.
--
-- coins.txt holds each currency's coins at the byte range its row gives:
--   <base unit>|<era from>|<era to>
--   <id>|<name>|<symbol>|<value>|<valid from>|<valid to>|<periods>   per coin
-- where periods is "<from>:<to>:<value>" entries joined by ",". Numbers keep
-- their JSON text.

-- Not require(), which would keep jsonstream loaded after the build.
local JSONSTREAM = "/apps/.lib/jsonstream.lua"
local jsonstream = assert(load(assert(fs.read(JSONSTREAM), "Missing " .. JSONSTREAM), "@jsonstream.lua"))()

local KINDS = { { "length", "Length" }, { "area", "Area" }, { "volume", "Volume" }, { "mass", "Mass" },
  { "count", "Count" } }
local METRIC = { ["Metric (SI)"] = true, ["Metric"] = true, ["Metric-derived"] = true }
local FIRST_SYSTEMS = { "Metric", "English / Imperial / US customary", "Nautical", "Astronomical", "Counting" }
local LAST_SYSTEMS = { "Planck (natural units)", "Novelty" }
local GROUPS = {
  { "British Isles", "gbp scots iep_old" },
  { "Americas", "usd spd" },
  { "France", "livre frf frf_new" },
  { "German lands", "thaler mark reichsmark dem" },
  { "Austria", "aut_gulden aut_krone" },
  { "Italy", "florin_fl ducat_ve itl" },
  { "Rest of Europe", "nlg pte_real rub_imp chf sek esp eur" },
  { "Asia", "ryo jpy tael cny inr" },
  { "Ottoman", "akce kurus" },
  { "Antiquity", "denarius solidus drachma_att shekel" },
  { "Fictional", "dnd westeros sw_credit latinum ankh_morpork triganic_pu" },
}

local function clean(s)
  return (tostring(s or ""):gsub("[|\n]", "/"))
end

local function system_rank(name)
  for i, s in ipairs(FIRST_SYSTEMS) do
    if s == name then return string.format("0%02d", i) end
  end
  for i, s in ipairs(LAST_SYSTEMS) do
    if s == name then return string.format("3%02d", i) end
  end
  return (name:lower():find("fictional") and "2" or "1") .. name
end

-- Fixed-width text that sorts like the number: log10 of a JSON number, offset
-- to stay positive, computed from its digits so tiny values survive 32-bit floats.
local function size_key(text)
  local int, frac, exp = text:match("^%-?(%d*)%.?(%d*)[eE]?([-+]?%d*)$")
  local digits = (int or "") .. (frac or "")
  local nz = digits:find("[1-9]")
  if not nz then return "0000000.00000" end
  local m = tonumber(digits:sub(nz, nz) .. "." .. digits:sub(nz + 1, nz + 6))
  local e = #int - nz + (tonumber(exp) or 0)
  return string.format("%013.5f", 500 + e + math.log(m, 10))
end

-- One pass over units.slim.json per kind keeps a single kind's rows in memory.
local function write_units(units_path, out)
  local base = {}
  local r = assert(jsonstream.open(units_path))
  r:object(function(key)
    if key == "baseUnits" then base = r:value() else r:skip() end
  end)
  for _, kind in ipairs(KINDS) do
    local systems = {}
    r = assert(jsonstream.open(units_path))
    r:object(function(key)
      if key ~= "units" then return r:skip() end
      r:array(function()
        local at = r:offset()
        local u = r:value()
        if u.category ~= kind[1] then return end
        local system = METRIC[u.system] and "Metric" or clean(u.system)
        systems[system] = systems[system] or {}
        local list = systems[system]
        list[#list + 1] = size_key(u.si) .. "|" .. at .. "|" .. clean(u.id) .. "|" .. clean(u.name)
      end)
    end)
    local names = {}
    for name in pairs(systems) do names[#names + 1] = name end
    if #names > 0 then
      table.sort(names, function(a, b) return system_rank(a) < system_rank(b) end)
      fs.append(out, "K|" .. kind[1] .. "|" .. kind[2] .. "|" .. clean(base[kind[1]]) .. "\n")
      for _, name in ipairs(names) do
        local list = systems[name]
        table.sort(list)
        for i, line in ipairs(list) do list[i] = line:sub(15) end -- drop the size key
        fs.append(out, "S|" .. name .. "\n" .. table.concat(list, "\n") .. "\n")
      end
    end
  end
end

local function coin_block(c)
  local rows = { clean(c.base_unit) .. "|" .. clean(c.era_text[1]) .. "|" .. clean(c.era_text[2]) }
  for _, den in ipairs(c.denoms or {}) do
    local periods = {}
    for _, p in ipairs(den.periods or {}) do
      periods[#periods + 1] = clean(p.from) .. ":" .. clean(p.to) .. ":" .. clean(p.value)
    end
    rows[#rows + 1] = table.concat({ clean(den.id), clean(den.name), clean(den.symbol), clean(den.value),
      clean(den.validFrom), clean(den.validTo), table.concat(periods, ",") }, "|")
  end
  return table.concat(rows, "\n") .. "\n"
end

local function write_money(money_path, out, coins_path)
  local group_of = {}
  for gi, g in ipairs(GROUPS) do
    for id in g[2]:gmatch("%S+") do group_of[id] = gi end
  end
  local lists = {}
  local coins_size = 0
  assert(fs.write(coins_path, ""))
  local r = assert(jsonstream.open(money_path))
  r:object(function(key)
    if key ~= "currencies" then return r:skip() end
    r:array(function()
      local c = { offset = r:offset(), era = 0, era_text = {} }
      r:object(function(k)
        if k == "id" or k == "name" or k == "baseUnit" then
          c[k == "baseUnit" and "base_unit" or k] = r:string()
        elseif k == "denominations" then
          c.denoms = r:value()
        elseif k == "era" then
          c.era_text = r:value()
          c.era = tonumber(c.era_text[1] or "0") or 0
        elseif k == "fictional" then
          c.fictional = r:value() == true
        elseif k == "series" then
          r:array(function()
            r:object(function(pk)
              if pk ~= "year" then return r:skip() end
              local y = tonumber(r:number())
              c.first, c.last = c.first or y, y
            end)
          end)
        else
          r:skip()
        end
      end)
      local kind = not c.first and "n" or (c.fictional and "f" or "r")
      local block = coin_block(c)
      assert(fs.append(coins_path, block))
      local gi = group_of[c.id] or #GROUPS + 1
      lists[gi] = lists[gi] or {}
      local list = lists[gi]
      list[#list + 1] = string.format("%07d|%d|%s|%s|%s|%s|%d|%d|%s", c.era + 5000, c.offset, clean(c.id),
        clean(c.name), kind == "r" and c.first or "", kind == "r" and c.last or "", coins_size, #block, kind)
      coins_size = coins_size + #block
    end)
  end)
  for gi = 1, #GROUPS + 1 do
    local list = lists[gi]
    if list then
      table.sort(list)
      for i, line in ipairs(list) do list[i] = line:sub(9) end -- drop the era key
      fs.append(out, "G|" .. (GROUPS[gi] and GROUPS[gi][1] or "Other") .. "\n" .. table.concat(list, "\n") .. "\n")
    end
  end
end

local HEADER_WIDTH = 63

local function header(text)
  return text .. string.rep(" ", HEADER_WIDTH - #text) .. "\n"
end

-- Writes out and coins_path (replacing them). The stamp line is written last,
-- in place, so an interrupted build is never mistaken for a finished one.
return function(units_path, money_path, out, coins_path, stamp)
  assert(#stamp <= HEADER_WIDTH, "stamp too long")
  assert(fs.write(out, header("building")))
  write_units(units_path, out)
  write_money(money_path, out, coins_path)
  assert(fs.write(out, header(stamp), 0))
end
