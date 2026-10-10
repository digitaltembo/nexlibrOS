-- A currency's coins from coins.txt (format in catalog.lua),
-- their values, and number formatting. Loaded through ui.lua's C.use() by
-- pickers.lua and money.lua, and released with them. Chunk argument: ui.lua's
-- shared table C.

local C = ...
local big, num = C.big, C.num

local M = {}

local function field(s)
  return s ~= "" and s or nil
end

-- { base_unit, era_from, era_to, fictional, denoms } for catalog currency c.
-- Coin fields keep the JSON's names and number text.
function M.read(c)
  local block = fs.read(C.COINS, c.coins_offset, c.coins_length)
  if not block or #block ~= c.coins_length then error("Could not read " .. C.COINS, 0) end
  local d = { denoms = {}, fictional = c.kind == "f" }
  local first = true
  for line in block:gmatch("[^\n]+") do
    local f = {}
    for v in (line .. "|"):gmatch("([^|]*)|") do f[#f + 1] = v end
    if first then
      d.base_unit, d.era_from, d.era_to = f[1], num(field(f[2])) or 0, num(field(f[3]))
      first = false
    else
      local den = { id = f[1], name = f[2], symbol = field(f[3]), value = field(f[4]), validFrom = field(f[5]),
        validTo = field(f[6]) }
      if f[7] ~= "" then
        den.periods = {}
        for from, to, value in f[7]:gmatch("([^:,]*):([^:,]*):([^:,]*)") do
          den.periods[#den.periods + 1] = { from = from, to = to, value = value }
        end
      end
      d.denoms[#d.denoms + 1] = den
    end
  end
  return d
end

function M.base_denomination(d)
  for _, den in ipairs(d.denoms) do
    if not den.periods and num(den.value) == 1 then return den end
  end
  return { id = d.base_unit, name = d.base_unit, value = "1" }
end

-- Value in base units; outside every period, the nearest period's value.
function M.denom_value(den, year)
  if den.value then return big(den.value) end
  local best, best_dist
  for _, p in ipairs(den.periods or {}) do
    local dist = 0
    if year then
      local pf, pt = num(p.from), num(p.to)
      dist = year < pf and pf - year or (year > pt and year - pt or 0)
      if dist == 0 then return big(p.value) end
    end
    if not best or dist < best_dist then best, best_dist = p, dist end
  end
  return best and big(best.value) or { 1, 0 }
end

function M.in_use(d, den, year)
  if not year then return true end
  local vf, vt = num(den.validFrom), num(den.validTo)
  if (vf and year < vf) or (vt and year > vt) then return false end
  if den.periods then
    local inside = false
    for _, p in ipairs(den.periods) do
      if num(p.from) <= year and year <= num(p.to) then inside = true end
    end
    if not inside then return false end
  end
  if d.fictional then return true end
  return year >= d.era_from - 50 and (not d.era_to or year <= d.era_to + 50)
end

function M.log10(a)
  return math.log(math.abs(a[1]), 10) + a[2]
end

local function group_thousands(int)
  return (int:reverse():gsub("(%d%d%d)", "%1,"):reverse():gsub("^,", ""))
end

-- sig significant digits, built from integer digits so float rounding never shows.
function M.fmt(a, sig)
  sig = sig or 6
  local m, e = a[1], a[2]
  if m == 0 then return "0" end
  local sign = m < 0 and "-" or ""
  local r = math.floor(math.abs(m) * 10.0 ^ (sig - 1) + 0.5)
  if r >= 10 ^ sig then r, e = r // 10, e + 1 end
  local digits = string.format("%d", r)
  if e >= 12 or e < -5 then
    local rest = digits:sub(2):gsub("0+$", "")
    return sign .. digits:sub(1, 1) .. (rest ~= "" and "." .. rest or "") .. " × 10^" .. e
  end
  local int, frac
  if e >= sig - 1 then
    int, frac = digits .. string.rep("0", e - sig + 1), ""
  elseif e >= 0 then
    int, frac = digits:sub(1, e + 1), digits:sub(e + 2)
  else
    int, frac = "0", string.rep("0", -e - 1) .. digits
  end
  frac = frac:gsub("0+$", "")
  return sign .. group_thousands(int) .. (frac ~= "" and "." .. frac or "")
end

return M
