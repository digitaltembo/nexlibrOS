-- Holidays for the calendar app, computed from rules, plus recurring personal
-- dates read from a file. Day numbers count days since 1970-01-01.
--
--   local holiday = require("holiday")
--   holiday.init({ to_days = ..., from_days = ..., weekday = ..., days_in_month = ... })
--   holiday.load("dates.txt")
--   holiday.on(z) -- { name, ... } or nil

local lines = require("lines")

local holiday = {}

local to_days, from_days, weekday, days_in_month

-- Takes the calendar's date helpers.
function holiday.init(dates)
  to_days, from_days, weekday, days_in_month = dates.to_days, dates.from_days, dates.weekday, dates.days_in_month
end

local holiday_year, holiday_days = nil, nil -- the year last computed: day number -> { name, ... }
local recurring = {} -- month * 100 + day -> { { name = ..., year = ... | nil }, ... }

-- Reads recurring dates, one per line: "MM-DD Name" repeats every year;
-- "YYYY-MM-DD Name" repeats from YYYY and shows the count of years, e.g.
-- "Sam's birthday (30)". Other lines (such as "# comments") are ignored.
function holiday.load(path)
  recurring, holiday_year = {}, nil
  lines.each(path, function(line)
    local y, m, d, name = line:match("^%s*(%d%d%d%d)-(%d%d?)-(%d%d?)%s+(.-)%s*$")
    if not y then m, d, name = line:match("^%s*(%d%d?)-(%d%d?)%s+(.-)%s*$") end
    m, d = tonumber(m), tonumber(d)
    if not m or m < 1 or m > 12 or d < 1 or d > days_in_month(2000, m) or name == "" then return end
    local key = m * 100 + d
    local list = recurring[key] or {}
    list[#list + 1] = { name = name, year = tonumber(y) }
    recurring[key] = list
  end)
end

local function nth_weekday(y, m, wd, n)
  if n < 0 then
    local last = to_days(y, m, days_in_month(y, m))
    return last - (weekday(last) - wd) % 7
  end
  local first = to_days(y, m, 1)
  return first + (wd - weekday(first)) % 7 + (n - 1) * 7
end

local function add_holiday(days, z, name)
  local list = days[z] or {}
  list[#list + 1] = name
  days[z] = list
end

-- US rule: a fixed-date holiday on Saturday is observed the Friday before, on
-- Sunday the Monday after; the observed day may fall in the neighbouring year.
local function add_us_fixed(days, y, m, d, name)
  local z = to_days(y, m, d)
  add_holiday(days, z, name)
  local wd = weekday(z)
  if wd == 6 then add_holiday(days, z - 1, name .. " (observed)") end
  if wd == 0 then add_holiday(days, z + 1, name .. " (observed)") end
end

-- Gregorian Easter Sunday (anonymous Gregorian algorithm).
local function easter(y)
  local a, b, c = y % 19, y // 100, y % 100
  local h = (19 * a + b - b // 4 - (b - (b + 8) // 25 + 1) // 3 + 15) % 30
  local l = (32 + 2 * (b % 4) + 2 * (c // 4) - h - c % 4) % 7
  local k = (a + 11 * h + 22 * l) // 451
  local n = h + l - 7 * k + 114
  return to_days(y, n // 31, n % 31 + 1)
end

-- Day number of 1 Tishri (Rosh Hashanah) of Hebrew year hy, using the
-- arithmetic Hebrew calendar (molad of Tishri and the postponement rules).
local function hebrew_elapsed(hy)
  local months = (235 * hy - 234) // 19
  local days = 29 * months + (12084 + 13753 * months) // 25920
  if (3 * (days + 1)) % 7 < 3 then days = days + 1 end
  return days
end

local function hebrew_new_year(hy)
  local ny0, ny1, ny2 = hebrew_elapsed(hy - 1), hebrew_elapsed(hy), hebrew_elapsed(hy + 1)
  local correction = (ny2 - ny1 == 356) and 2 or ((ny1 - ny0 == 382) and 1 or 0)
  return ny1 + correction - 2092590 -- Hebrew epoch relative to 1970-01-01
end

-- 29 February dates fall on the 28th in other years.
local function year_recurring(y, days)
  for key, list in pairs(recurring) do
    local m = key // 100
    local z = to_days(y, m, math.min(key % 100, days_in_month(y, m)))
    for _, r in ipairs(list) do
      if not r.year or y == r.year then
        add_holiday(days, z, r.name)
      elseif y > r.year then
        add_holiday(days, z, r.name .. " (" .. (y - r.year) .. ")")
      end
    end
  end
end

local function year_holidays(y, days)
  -- US federal (5 U.S.C. 6103).
  add_us_fixed(days, y, 1, 1, "New Year's Day")
  add_holiday(days, nth_weekday(y, 1, 1, 3), "Martin Luther King Jr. Day")
  add_holiday(days, nth_weekday(y, 2, 1, 3), "Washington's Birthday")
  add_holiday(days, nth_weekday(y, 5, 1, -1), "Memorial Day")
  add_us_fixed(days, y, 6, 19, "Juneteenth")
  add_us_fixed(days, y, 7, 4, "Independence Day")
  add_holiday(days, nth_weekday(y, 9, 1, 1), "Labor Day")
  add_holiday(days, nth_weekday(y, 10, 1, 2), "Columbus Day")
  add_us_fixed(days, y, 11, 11, "Veterans Day")
  add_holiday(days, nth_weekday(y, 11, 4, 4), "Thanksgiving Day")
  add_us_fixed(days, y, 12, 25, "Christmas Day")
  if y % 4 == 1 then
    -- Moved to the 21st when the 20th is a Sunday.
    local z = to_days(y, 1, 20)
    add_holiday(days, weekday(z) == 0 and z + 1 or z, "Inauguration Day")
  end

  -- Canadian federal statutory holidays not already listed above (New Year's
  -- Day, Labour Day and Christmas Day share the US dates).
  local may24 = to_days(y, 5, 24)
  add_holiday(days, may24 - (weekday(may24) - 1) % 7, "Victoria Day")
  add_holiday(days, to_days(y, 7, 1), "Canada Day")
  add_holiday(days, to_days(y, 9, 30), "Truth and Reconciliation Day")
  add_holiday(days, nth_weekday(y, 10, 1, 2), "Thanksgiving (Canada)")
  add_holiday(days, to_days(y, 11, 11), "Remembrance Day")
  add_holiday(days, to_days(y, 12, 26), "Boxing Day")

  -- Christian.
  local e = easter(y)
  add_holiday(days, e - 46, "Ash Wednesday")
  add_holiday(days, e - 2, "Good Friday")
  add_holiday(days, e, "Easter Sunday")
  add_holiday(days, e + 1, "Easter Monday")

  -- Jewish, on the first full day (each begins at sundown the evening before).
  local hy = y + 3761 -- the Hebrew year starting in this year's autumn
  local rosh = hebrew_new_year(hy)
  local length = hebrew_new_year(hy + 1) - rosh
  add_holiday(days, rosh - 163, "Passover")
  add_holiday(days, rosh, "Rosh Hashanah")
  add_holiday(days, rosh + 9, "Yom Kippur")
  -- 25 Kislev; Heshvan has 30 days in a 355- or 385-day year, otherwise 29.
  add_holiday(days, rosh + 30 + (length % 10 == 5 and 30 or 29) + 24, "Hanukkah")
end

function holiday.on(z)
  local y = from_days(z)
  if holiday_year ~= y then
    holiday_year, holiday_days = y, {}
    year_recurring(y, holiday_days) -- listed before the holidays on the same day
    for year = y - 1, y + 1 do year_holidays(year, holiday_days) end
  end
  return holiday_days[z]
end

return holiday
