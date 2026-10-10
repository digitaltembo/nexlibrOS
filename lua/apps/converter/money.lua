-- Converter conversion, loaded by ui.lua's C.use() for each conversion and
-- released with jsonstream and coins.lua afterwards. Chunk argument: ui.lua's
-- shared table C. Returns convert().
--
-- Unit records and currency value series are streamed from the JSON: the
-- catalog gives each record's byte offset, and only that record is decoded.

local C = ...
local S = C.S
local big, norm, num, year_text = C.big, C.norm, C.num, C.year_text
local jsonstream = C.use("/apps/.lib/jsonstream.lua")
local coins = C.use("coins.lua")
local fmt, log10, denom_value = coins.fmt, coins.log10, coins.denom_value

local PRECISION_RANK = { exact = 1, approximate = 2, uncertain = 3 }
local CONFIDENCE_RANK = { high = 1, medium = 2, low = 3, ["very low"] = 4 }

local function mul(a, b) return norm(a[1] * b[1], a[2] + b[2]) end
local function div(a, b) return norm(a[1] / b[1], a[2] - b[2]) end

local function lerp_log(a, b, t)
  local l = log10(a) + (log10(b) - log10(a)) * t
  local e = math.floor(l)
  return norm(10.0 ^ (l - e), e)
end

local function load_unit(sel)
  return assert(jsonstream.open(C.UNITS, sel.offset)):value()
end

-- The value series points around year: a (at or before), b (after), and the
-- first point, which is the only one fictional currencies use.
local function load_series(c, year)
  local r = assert(jsonstream.open(C.MONEY, c.offset))
  local d = { year = year, fictional = c.kind == "f" }
  r:object(function(k)
    if k ~= "series" then return r:skip() end
    r:array(function()
      local p = r:value()
      p.year = tonumber(p.year)
      d.first_point = d.first_point or p
      if year and p.year <= year then
        d.a = p
      elseif year and not d.b then
        d.b = p
      end
    end)
  end)
  return d
end

local function worse_confidence(a, b)
  return (CONFIDENCE_RANK[a] or 4) >= (CONFIDENCE_RANK[b] or 4) and a or b
end

-- Value of one base unit in 2025 USD: log-linear between data points,
-- the nearest point outside them, the only point for fictional coins.
local function value_at(d)
  local p
  if d.fictional or not d.year then
    p = d.first_point
  elseif not d.a then
    p = d.b
  elseif d.a.year == d.year or not d.b then
    p = d.a
  end
  if p then return { usd = big(p.usd2025), low = big(p.low), high = big(p.high), conf = p.confidence } end
  local a, b = d.a, d.b
  local t = (d.year - a.year) / (b.year - a.year)
  return {
    usd = lerp_log(big(a.usd2025), big(b.usd2025), t),
    low = lerp_log(big(a.low), big(b.low), t),
    high = lerp_log(big(a.high), big(b.high), t),
    conf = worse_confidence(a.confidence, b.confidence),
  }
end

local function range_line(low, high, sig)
  local lo, hi = fmt(low, sig), fmt(high, sig)
  return lo ~= hi and ("Range " .. lo .. " to " .. hi) or nil
end

local function convert_units()
  local f = load_unit(S.from)
  local t = load_unit(S.to)
  local fsi, tsi = big(f.si), big(t.si)
  local a = S.amount.value
  local value = div(mul(a, fsi), tsi)
  local precision = (PRECISION_RANK[f.precision] or 3) >= (PRECISION_RANK[t.precision] or 3) and f.precision
      or t.precision
  local out = { main = fmt(value, 6), label = t.name, lines = {} }
  if precision ~= "exact" then
    out.lines[#out.lines + 1] = range_line(div(mul(a, big(f.low) or fsi), big(t.high) or tsi),
      div(mul(a, big(f.high) or fsi), big(t.low) or tsi), 4)
  end
  out.lines[#out.lines + 1] = "Precision: " .. tostring(precision)
  return out
end

local function convert_money()
  local from, to = S.from, S.to
  local base = mul(S.amount.value, denom_value(from.denom, from.year))
  local tdv = denom_value(to.denom, to.year)
  local label = to.denom.name .. (to.year and (", " .. year_text(to.year)) or "")
  if from.cur.kind == "n" or to.cur.kind == "n" then
    return { main = fmt(div(base, tdv), 6), label = label, lines = { "No dollar value: coin ratio only" } }
  end
  local fv = value_at(load_series(from.cur, from.year))
  local tv = value_at(load_series(to.cur, to.year))
  local out = { main = fmt(div(mul(base, fv.usd), mul(tv.usd, tdv)), 5), label = label, lines = {} }
  out.lines[#out.lines + 1] = range_line(div(mul(base, fv.low), mul(tv.high, tdv)),
    div(mul(base, fv.high), mul(tv.low, tdv)), 3)
  out.lines[#out.lines + 1] = "Confidence: " .. worse_confidence(fv.conf, tv.conf)
  if not (to.cur.id == "usd" and to.year == to.cur.last and num(to.denom.value) == 1) then
    out.lines[#out.lines + 1] = "Worth $" .. fmt(mul(base, fv.usd), 4) .. " in 2025 US dollars"
  end
  return out
end

-- The result for S.amount, S.from and S.to: { main, label, lines }.
return function()
  return S.from.kind == "unit" and convert_units() or convert_money()
end
