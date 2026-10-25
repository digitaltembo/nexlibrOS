-- title: Calendar
--
-- US, Canadian, Christian and Jewish holidays are shown in bold
-- above a day's notes, after any recurring dates (birthdays, anniversaries)
-- listed in the app's dates.txt, one per line:
--   03-14 Pi Day                  every year
--   1996-07-02 Sam's birthday     every year from 1996, as "Sam's birthday (30)"
-- A 29 February date falls on the 28th in other years. dates.txt is read when
-- the app starts.
--
-- Notes are kept one file per month in the app's notes/ folder,
-- <yyyy>-<mm>.txt, one note per line: <day><TAB><text>.
--
-- Grid: arrows move the selected date, Confirm opens the day. Up from the
-- top row focuses the header (month, year, Today, view tabs); Left/Right move
-- along it and Confirm picks. Day view: Confirm on a note edits or deletes it.
-- Touch: tap the month or year to pick another, tap a date twice to open it,
-- swipe left/right for the next/previous month, week or day.

local listview = require("listview")

local NOTES_DIR = "notes"
local DATES_FILE = "dates.txt"
local MARGIN = 16
local MAX_NOTE = 200
local WEEK_START = 0 -- 0 = Sunday, 1 = Monday
local CACHE_MONTHS = 6
local YEARS_PER_PAGE = 12
local MIN_YEAR, MAX_YEAR = 1, 9999

local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
  "October", "November", "December" }
local WEEKDAYS = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }

-- Header items in focus order.
local HEADER = { "month", "year", "today", "tab_month", "tab_week", "tab_day" }
local TABS = { { id = "tab_month", view = "month", label = "Month" }, { id = "tab_week", view = "week", label = "Week" },
  { id = "tab_day", view = "day", label = "Day" } }
local NOTE_MENU = { "Edit note", "Delete note", "Cancel" }

local view = "month"
local sel = 0 -- selected date as a day number (days since 1970-01-01)
local today = 0
local focus = "grid" -- "grid" or a HEADER id
local picker = nil -- { kind = "month" | "year", index, base }
local menu = nil -- { note = index, list = listview }
local day_list = listview.new(64)
local message = nil
local hits = {} -- touch targets recorded during draw(): { x, y, w, h, fn }

-- Dates ------------------------------------------------------------------------

-- Day numbers relative to 1970-01-01 (proleptic Gregorian), so dates far from
-- the present stay exact under 32-bit integers, unlike os.time().
local function to_days(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = y // 400
  local yoe = y - era * 400
  local doy = (153 * ((m + 9) % 12) + 2) // 5 + d - 1
  return era * 146097 + yoe * 365 + yoe // 4 - yoe // 100 + doy - 719468
end

local function from_days(z)
  z = z + 719468
  local era = z // 146097
  local doe = z - era * 146097
  local yoe = (doe - doe // 1460 + doe // 36524 - doe // 146096) // 365
  local doy = doe - (365 * yoe + yoe // 4 - yoe // 100)
  local mp = (5 * doy + 2) // 153
  local d = doy - (153 * mp + 2) // 5 + 1
  local m = mp < 10 and mp + 3 or mp - 9
  return yoe + era * 400 + (m <= 2 and 1 or 0), m, d
end

-- 0 = Sunday; 1970-01-01 was a Thursday.
local function weekday(z)
  return (z + 4) % 7
end

local function days_in_month(y, m)
  if m == 2 then
    return (y % 4 == 0 and (y % 100 ~= 0 or y % 400 == 0)) and 29 or 28
  end
  return (m == 4 or m == 6 or m == 9 or m == 11) and 30 or 31
end

local function week_start(z)
  return z - (weekday(z) - WEEK_START) % 7
end

local function set_date(y, m, d)
  y = math.max(MIN_YEAR, math.min(MAX_YEAR, y))
  sel = to_days(y, m, math.min(d, days_in_month(y, m)))
end

local function move_days(n)
  local z = sel + n
  local y = from_days(z)
  if y >= MIN_YEAR and y <= MAX_YEAR then sel = z end
end

local function move_months(n)
  local y, m, d = from_days(sel)
  local index = y * 12 + (m - 1) + n
  set_date(index // 12, index % 12 + 1, d)
end

local function refresh_today()
  local t = os.date("*t")
  today = to_days(t.year, t.month, t.day)
end

local holiday = require("holiday")
holiday.init({ to_days = to_days, from_days = from_days, weekday = weekday, days_in_month = days_in_month })
holiday.load(DATES_FILE)

-- Notes ------------------------------------------------------------------------

local cache = {} -- "yyyy-mm" -> { [day] = { text, ... } }
local cached = 0

local function month_key(y, m)
  return string.format("%04d-%02d", y, m)
end

local function month_notes(y, m)
  local key = month_key(y, m)
  local notes = cache[key]
  if notes then return notes end
  if cached >= CACHE_MONTHS then cache, cached = {}, 0 end
  notes = {}
  local text = fs.read(NOTES_DIR .. "/" .. key .. ".txt")
  if text then
    for day, note in text:gmatch("(%d+)\t([^\n]*)\n?") do
      local list = notes[tonumber(day)] or {}
      list[#list + 1] = note
      notes[tonumber(day)] = list
    end
  end
  cache[key], cached = notes, cached + 1
  return notes
end

local function notes_on(z)
  local y, m, d = from_days(z)
  return month_notes(y, m)[d] or {}
end

local function save_month(y, m)
  local notes = month_notes(y, m)
  local lines = {}
  for d = 1, 31 do
    for _, note in ipairs(notes[d] or {}) do lines[#lines + 1] = d .. "\t" .. note end
  end
  local path = NOTES_DIR .. "/" .. month_key(y, m) .. ".txt"
  if #lines == 0 then
    fs.remove(path)
    return
  end
  local ok, err = fs.write(path, table.concat(lines, "\n") .. "\n")
  if not ok then message = "Save failed: " .. tostring(err) end
end

-- Replaces note index on date z with text; index nil appends, text "" deletes.
local function set_note(z, index, text)
  local y, m, d = from_days(z)
  local notes = month_notes(y, m)
  local list = notes[d] or {}
  if text == "" then
    if index then table.remove(list, index) end
  else
    list[index or #list + 1] = text
  end
  notes[d] = #list > 0 and list or nil
  save_month(y, m)
end

local function edit_note(z, index)
  local initial = index and notes_on(z)[index] or ""
  sys.keyboard(index and "Edit note" or "New note", initial, function(text)
    if text then
      -- Newlines and tabs would break the one-line-per-note file format.
      set_note(z, index, (text:gsub("[\r\n\t]+", " ")))
    end
    sys.redraw()
  end, MAX_NOTE)
end

-- Navigation -------------------------------------------------------------------

local function set_view(v)
  view = v
  if v == "day" then day_list.selected, day_list.top = 1, 1 end
end

local function open_picker(kind)
  local y, m = from_days(sel)
  if kind == "month" then
    picker = { kind = "month", index = m }
  else
    local base = y - (y - MIN_YEAR) % YEARS_PER_PAGE
    picker = { kind = "year", index = y - base + 1, base = base }
  end
end

local function activate_header(id)
  if id == "month" or id == "year" then
    open_picker(id)
  elseif id == "today" then
    refresh_today()
    sel = today
  else
    for _, tab in ipairs(TABS) do
      if tab.id == id then set_view(tab.view) end
    end
  end
end

local function header_move(delta)
  for i, id in ipairs(HEADER) do
    if id == focus then
      focus = HEADER[(i - 1 + delta) % #HEADER + 1]
      return
    end
  end
end

-- Moves the picker selection; year pages shift when it runs off either end.
local function picker_move(delta)
  local p = picker
  if p.kind == "month" then
    p.index = math.max(1, math.min(12, p.index + delta))
    return
  end
  local year = p.base + p.index - 1 + delta
  year = math.max(MIN_YEAR, math.min(MAX_YEAR, year))
  while year < p.base do p.base = p.base - YEARS_PER_PAGE end
  while year >= p.base + YEARS_PER_PAGE do p.base = p.base + YEARS_PER_PAGE end
  p.index = year - p.base + 1
end

local function picker_page(direction)
  local p = picker
  if p.kind ~= "year" then return end
  local base = p.base + direction * YEARS_PER_PAGE
  if base + YEARS_PER_PAGE - 1 < MIN_YEAR or base > MAX_YEAR then return end
  p.base = base
  p.index = math.max(MIN_YEAR - base + 1, math.min(p.index, MAX_YEAR - base + 1))
end

local function picker_choose(index)
  local p = picker
  local y, m, d = from_days(sel)
  if p.kind == "month" then
    set_date(y, index, d)
  else
    set_date(p.base + index - 1, m, d)
  end
  picker = nil
end

local function open_day(z)
  sel = z
  set_view("day")
  focus = "grid"
end

local function day_activate(row)
  local notes = notes_on(sel)
  if row > #notes then
    edit_note(sel)
  else
    menu = { note = row, list = listview.new(52) }
  end
end

local function menu_choose(row)
  local index = menu.note
  menu = nil
  if row == 1 then
    edit_note(sel, index)
  elseif row == 2 then
    set_note(sel, index, "")
  end
end

-- Moves the selection in the active view by one step in a direction
-- ("up", "down", "left", "right").
local function grid_move(button)
  if view == "month" then
    if button == "left" then
      move_days(-1)
    elseif button == "right" then
      move_days(1)
    elseif button == "down" then
      move_days(7)
    elseif button == "up" then
      local y, m = from_days(sel)
      if sel - 7 < to_days(y, m, 1) then focus = "month" else move_days(-7) end
    end
  elseif view == "week" then
    if button == "left" then
      move_days(-7)
    elseif button == "right" then
      move_days(7)
    elseif button == "down" then
      if sel < week_start(sel) + 6 then move_days(1) end
    elseif button == "up" then
      if sel == week_start(sel) then focus = "month" else move_days(-1) end
    end
  else
    local count = #notes_on(sel) + 1
    if button == "left" then
      move_days(-1)
      day_list.selected = 1
    elseif button == "right" then
      move_days(1)
      day_list.selected = 1
    elseif button == "down" then
      day_list.selected = math.min(count, day_list.selected + 1)
    elseif button == "up" then
      if day_list.selected <= 1 then focus = "month" else day_list.selected = day_list.selected - 1 end
    end
  end
end

-- Drawing ----------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

local function hit(x, y, w, h, fn)
  hits[#hits + 1] = { x = x, y = y, w = w, h = h, fn = fn }
end

-- A header item: inverted when focused, outlined when it is the active tab.
local function draw_item(x, y, label, focused, outlined)
  local tw = gfx.text_width(label)
  local lh = gfx.line_height()
  local w, h = tw + 16, lh + 8
  if focused then
    gfx.fill_round_rect(x, y, w, h, 6)
  elseif outlined then
    gfx.round_rect(x, y, w, h, 6)
    gfx.round_rect(x + 1, y + 1, w - 2, h - 2, 6)
  end
  gfx.text(x + 8, y + 4, label, not focused)
  return w, h
end

local function draw_header(w)
  local y, m = from_days(sel)
  local top = MARGIN

  gfx.font("ui_large", "bold")
  local mw, h = draw_item(MARGIN - 8, top, MONTHS[m], focus == "month")
  hit(MARGIN - 8, top, mw, h, function() open_picker("month") end)
  local yw = draw_item(MARGIN - 8 + mw, top, tostring(y), focus == "year")
  hit(MARGIN - 8 + mw, top, yw, h, function() open_picker("year") end)

  gfx.font("ui")
  local label = "Today"
  local tx = w - MARGIN + 8 - (gfx.text_width(label) + 16)
  local tw, th = draw_item(tx, top + (h - gfx.line_height() - 8) // 2, label, focus == "today", true)
  hit(tx, top, tw, h, function() activate_header("today") end)

  local row = top + h + 6
  local x = MARGIN - 8
  for _, tab in ipairs(TABS) do
    local iw, ih = draw_item(x, row, tab.label, focus == tab.id, view == tab.view)
    local v = tab.view
    hit(x, row, iw, ih, function() set_view(v) end)
    x = x + iw + 8
    th = ih
  end
  local bottom = row + th + 6
  gfx.line(MARGIN, bottom, w - MARGIN, bottom, true, 2)
  return bottom + 6
end

local function draw_selection(x, y, w, h)
  local width = focus == "grid" and 3 or 1
  for i = 0, width - 1 do gfx.rect(x + i, y + i, w - 2 * i, h - 2 * i) end
end

-- Holiday names (bold) then notes, one line each, ending with "+n" when they
-- do not all fit in max_lines.
local function draw_day_lines(holidays, notes, font, x, y, w, max_lines)
  holidays = holidays or {}
  local total = #holidays + #notes
  for i = 1, math.min(total, max_lines) do
    local is_holiday = i <= #holidays
    local text = is_holiday and holidays[i] or notes[i - #holidays]
    if i == max_lines and total > max_lines then text, is_holiday = "+" .. (total - max_lines + 1), false end
    gfx.font(font, is_holiday and "bold" or "regular")
    gfx.text(x, y + (i - 1) * gfx.line_height(), fit(text, w))
  end
end

local function draw_month(x, top, w, bottom)
  local y, m = from_days(sel)
  local first = to_days(y, m, 1)
  local lead = (weekday(first) - WEEK_START) % 7
  local count = days_in_month(y, m)
  local rows = (lead + count + 6) // 7
  local cw = w // 7

  gfx.font("small", "bold")
  local lh = gfx.line_height()
  for c = 0, 6 do
    local name = WEEKDAYS[(c + WEEK_START) % 7 + 1]:sub(1, 3)
    gfx.text(x + c * cw + (cw - gfx.text_width(name)) // 2, top, name)
  end
  top = top + lh + 4
  local ch = (bottom - top) // rows
  local notes = month_notes(y, m)

  for d = 1, count do
    local cell = lead + d - 1
    local cx, cy = x + (cell % 7) * cw, top + (cell // 7) * ch
    local z = first + d - 1
    if z == today then gfx.fill_rect(cx + 1, cy + 1, cw - 1, ch - 1, "light_gray") end
    gfx.rect(cx, cy, cw + 1, ch + 1)
    if z == sel then draw_selection(cx + 1, cy + 1, cw - 1, ch - 1) end
    gfx.font("ui", z == today and "bold" or "regular")
    gfx.text(cx + 6, cy + 4, tostring(d))
    local list, holidays = notes[d] or {}, holiday.on(z)
    if holidays or #list > 0 then
      gfx.font("small")
      local ny = cy + 4 + gfx.line_height() + 6
      local lines = (cy + ch - 4 - ny) // gfx.line_height()
      if lines >= 1 then
        draw_day_lines(holidays, list, "small", cx + 5, ny, cw - 10, lines)
      else
        gfx.fill_circle(cx + cw - 10, cy + 12, 4)
      end
    end
    hit(cx, cy, cw, ch, function(kind)
      if z == sel or kind == "long" then open_day(z) else sel = z end
    end)
  end
end

local function draw_week(x, top, w, bottom)
  local start = week_start(sel)
  local rh = (bottom - top) // 7
  gfx.font("ui", "bold")
  local label_w = gfx.text_width("Wed 30") + 16
  for i = 0, 6 do
    local z = start + i
    local _, m, d = from_days(z)
    local ry = top + i * rh
    if z == today then gfx.fill_rect(x + 1, ry + 1, w - 1, rh - 1, "light_gray") end
    gfx.rect(x, ry, w + 1, rh + 1)
    if z == sel then draw_selection(x + 1, ry + 1, w - 1, rh - 1) end
    gfx.font("ui", "bold")
    gfx.text(x + 8, ry + 6, WEEKDAYS[weekday(z) + 1]:sub(1, 3) .. " " .. d)
    gfx.font("small")
    gfx.text(x + 8, ry + 6 + gfx.line_height() + 6, MONTHS[m]:sub(1, 3))
    gfx.font("ui")
    draw_day_lines(holiday.on(z), notes_on(z), "ui", x + label_w, ry + 6, w - label_w - 8,
      math.max(1, (rh - 8) // gfx.line_height()))
    hit(x, ry, w, rh, function(kind)
      if z == sel or kind == "long" then open_day(z) else sel = z end
    end)
  end
end

local function draw_day(x, top, w, bottom)
  local y, m, d = from_days(sel)
  gfx.font("sans14", "bold")
  local title = WEEKDAYS[weekday(sel) + 1] .. ", " .. d .. " " .. MONTHS[m] .. " " .. y
  if sel == today then title = title .. " (today)" end
  gfx.text(x, top, fit(title, w))
  top = top + gfx.line_height() + 10
  gfx.font("ui", "bold")
  for _, name in ipairs(holiday.on(sel) or {}) do
    gfx.text(x, top, fit(name, w))
    top = top + gfx.line_height() + 4
  end

  local notes = notes_on(sel)
  if focus ~= "grid" then day_list.selected = math.min(day_list.selected, #notes + 1) end
  day_list:set_bounds(x, top, w, bottom - top)
  day_list:draw(#notes + 1, function(i, rx, ry, rw, rh, fg)
    if focus ~= "grid" and i == day_list.selected then
      -- Header focused: show the row outlined instead of selected.
      gfx.fill_rect(rx, ry + 2, rw, rh - 4, false)
      gfx.round_rect(rx, ry + 2, rw, rh - 4, 6)
      fg = true
    end
    gfx.font("ui")
    local lh = gfx.line_height()
    if i > #notes then
      gfx.text(rx + 12, ry + (rh - lh) // 2, "+ Add note", fg)
      return
    end
    local lines = gfx.wrap(notes[i], rw - 24, 2)
    local ty = ry + (rh - #lines * lh) // 2
    for j, line in ipairs(lines) do gfx.text(rx + 12, ty + (j - 1) * lh, line, fg) end
  end)
  hit(x, top, w, bottom - top, function(kind, tx, ty)
    local result, row = day_list:tap(tx, ty)
    if result == "row" then
      focus = "grid"
      day_activate(row)
    end
  end)
end

local function draw_picker(w, top, bottom)
  local p = picker
  local cols, rows = 3, 4
  local x = MARGIN
  local gw = w - 2 * MARGIN
  local nav_h = 0

  gfx.font("ui_large", "bold")
  local title = p.kind == "month" and "Select month" or "Select year"
  gfx.text(MARGIN, top, title)
  top = top + gfx.line_height() + 12

  if p.kind == "year" then
    gfx.font("ui", "bold")
    nav_h = gfx.line_height() + 16
    local earlier, later = "< Earlier", "Later >"
    local lw = gfx.text_width(later) + 16
    gfx.round_rect(x, top, gfx.text_width(earlier) + 16, nav_h - 8, 6)
    gfx.text(x + 8, top + 4, earlier)
    hit(x, top, gfx.text_width(earlier) + 16, nav_h, function() picker_page(-1) end)
    gfx.round_rect(x + gw - lw, top, lw, nav_h - 8, 6)
    gfx.text(x + gw - lw + 8, top + 4, later)
    hit(x + gw - lw, top, lw, nav_h, function() picker_page(1) end)
    top = top + nav_h
  end

  local cw, ch = gw // cols, (bottom - top) // rows
  local cur_y, cur_m = from_days(sel)
  gfx.font("ui", "bold")
  for i = 1, cols * rows do
    local label, current, valid
    if p.kind == "month" then
      label, current, valid = MONTHS[i], i == cur_m, true
    else
      local year = p.base + i - 1
      label, current, valid = tostring(year), year == cur_y, year >= MIN_YEAR and year <= MAX_YEAR
    end
    if valid then
      local cx, cy = x + (i - 1) % cols * cw, top + (i - 1) // cols * ch
      if i == p.index then
        gfx.fill_round_rect(cx + 4, cy + 4, cw - 8, ch - 8, 8)
      else
        gfx.round_rect(cx + 4, cy + 4, cw - 8, ch - 8, 8)
        if current then gfx.round_rect(cx + 5, cy + 5, cw - 10, ch - 10, 8) end
      end
      local text = fit(label, cw - 16)
      gfx.text(cx + (cw - gfx.text_width(text)) // 2, cy + (ch - gfx.line_height()) // 2, text, i ~= p.index)
      local index = i
      hit(cx, cy, cw, ch, function() picker_choose(index) end)
    end
  end
end

local function draw_menu(w, h)
  local list = menu.list
  gfx.font("ui")
  local mw = math.min(w - 2 * MARGIN, 360)
  local note = fit(notes_on(sel)[menu.note] or "", mw - 32)
  local lh = gfx.line_height()
  local mh = lh + 24 + #NOTE_MENU * list.row_h + 16
  local mx, my = (w - mw) // 2, (h - mh) // 2
  gfx.fill_round_rect(mx, my, mw, mh, 10, false)
  gfx.round_rect(mx, my, mw, mh, 10)
  gfx.round_rect(mx + 1, my + 1, mw - 2, mh - 2, 10)
  gfx.text(mx + 16, my + 12, note)
  list:set_bounds(mx + 8, my + lh + 24, mw - 16, #NOTE_MENU * list.row_h)
  list:draw(#NOTE_MENU, function(i, rx, ry, rw, rh, fg)
    gfx.font("ui")
    gfx.text(rx + 12, ry + (rh - gfx.line_height()) // 2, NOTE_MENU[i], fg)
  end)
end

local function hint_labels()
  if menu then return "Cancel", "Select", nil, nil end
  if picker then
    return "Cancel", "Select", "<", ">"
  end
  if focus ~= "grid" then return "Back", "Select", "<", ">" end
  if view == "day" then
    local notes = notes_on(sel)
    return "Month", day_list.selected > #notes and "Add" or "Options", "Prev day", "Next day"
  end
  if view == "week" then return "Month", "Open", "Prev week", "Next week" end
  return "Exit", "Open", "<", ">"
end

function draw()
  hits = {}
  refresh_today()
  local w, h = gfx.width(), gfx.height()
  local bottom = h - gfx.hints(hint_labels()) - 8

  if message then
    gfx.font("ui")
    bottom = bottom - gfx.line_height()
    gfx.text(MARGIN, bottom, fit(message, w - 2 * MARGIN))
    bottom = bottom - 8
  end

  if picker then
    draw_picker(w, MARGIN, bottom)
    return
  end

  local top = draw_header(w)
  local x, cw = MARGIN, w - 2 * MARGIN
  if view == "month" then
    draw_month(x, top, cw, bottom)
  elseif view == "week" then
    draw_week(x, top, cw, bottom)
  else
    draw_day(x, top, cw, bottom)
  end

  if menu then
    hits = {}
    draw_menu(w, h)
  end
end

-- Input ------------------------------------------------------------------------

local function picker_button(button)
  if button == "back" then
    picker = nil
  elseif button == "confirm" then
    picker_choose(picker.index)
  elseif button == "left" then
    picker_move(-1)
  elseif button == "right" then
    picker_move(1)
  elseif button == "up" then
    picker_move(-3)
  elseif button == "down" then
    picker_move(3)
  end
end

local function menu_button(button)
  if button == "back" then
    menu = nil
  elseif button == "up" or button == "left" then
    menu.list:move(-1)
  elseif button == "down" or button == "right" then
    menu.list:move(1)
  elseif button == "confirm" then
    menu_choose(menu.list.selected)
  end
end

local function header_button(button)
  if button == "back" or button == "down" then
    focus = "grid"
  elseif button == "left" then
    header_move(-1)
  elseif button == "right" then
    header_move(1)
  elseif button == "confirm" then
    local id = focus
    focus = "grid"
    activate_header(id)
  end
end

function on_button(button)
  message = nil
  if menu then
    menu_button(button)
  elseif picker then
    picker_button(button)
  elseif focus ~= "grid" then
    header_button(button)
  elseif button == "back" then
    if view == "month" then return false end -- leave the app
    set_view("month")
  elseif button == "confirm" then
    if view == "day" then day_activate(day_list.selected) else open_day(sel) end
  else
    grid_move(button)
  end
  sys.redraw()
  return true
end

function on_touch(x, y, kind)
  message = nil
  if menu then
    local result, row = menu.list:tap(x, y)
    if result == "row" then menu_choose(row) else menu = nil end
    sys.redraw()
    return
  end
  for i = #hits, 1, -1 do
    local r = hits[i]
    if x >= r.x and x < r.x + r.w and y >= r.y and y < r.y + r.h then
      focus = "grid"
      r.fn(kind, x, y)
      break
    end
  end
  sys.redraw()
end

function on_swipe(direction)
  if menu or (direction ~= "left" and direction ~= "right") then return end
  local step = direction == "left" and 1 or -1
  if picker then
    picker_page(step)
  elseif view == "month" then
    move_months(step)
  elseif view == "week" then
    move_days(7 * step)
  else
    move_days(step)
  end
  sys.redraw()
end

fs.mkdir(NOTES_DIR)
refresh_today()
sel = today
