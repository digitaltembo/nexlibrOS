-- title: Dictionary
-- permissions: read:/.dictionaries read:/dictionaries
--
-- Searches every StarDict dictionary folder under /.dictionaries and
-- /dictionaries (the ones the reader uses; see docs/dictionary.md).
--
-- Home: Confirm looks up a new word or the selected recent word.
-- Definition: Up/Down page, Left/Right switch between the dictionaries that
-- have the word, Confirm starts a new search, Back returns home.
-- Touch: swipe up/down (or tap the right/left half) to page, swipe left/right
-- to switch dictionaries, tap the title to go home; long-press for a new
-- search. On the home screen, swipe up/down to scroll the recent words.

local listview = require("listview")

local ROOTS = { "/.dictionaries", "/dictionaries" }
local HISTORY_FILE = "history.txt"
local MAX_HISTORY = 20
local MAX_WORD = 64
local MARGIN = 16
local ROW_HEIGHT = 52
local BODY_FONT = "serif14"

local dicts = {}   -- { name, path, needs_index }
local history = {} -- recent words, newest first
local home_view = listview.new(ROW_HEIGHT)
local result = nil -- { word, hits = { { dict, headword } }, index, lines, top }
local pending = nil -- word to look up on the next tick, after "Looking up..." is drawn
local message = nil
local header_bottom = 0
local page_lines = 1

-- Dictionaries and history ----------------------------------------------------

local function discover()
  dicts = {}
  for _, root in ipairs(ROOTS) do
    for _, entry in ipairs(fs.list(root) or {}) do
      local name = entry:match("^([^.].*)/$")
      if name then
        local path = root .. "/" .. name
        local status = dict.status(path)
        if status then
          dicts[#dicts + 1] = { name = name, path = path, needs_index = status == "needs_index" }
        end
      end
    end
  end
  table.sort(dicts, function(a, b) return a.name:lower() < b.name:lower() end)
end

local function load_history()
  history = {}
  for word in (fs.read(HISTORY_FILE) or ""):gmatch("[^\n]+") do
    if #history < MAX_HISTORY then history[#history + 1] = word end
  end
end

local function remember(word)
  for i = #history, 1, -1 do
    if history[i]:lower() == word:lower() then table.remove(history, i) end
  end
  table.insert(history, 1, word)
  while #history > MAX_HISTORY do table.remove(history) end
  fs.write(HISTORY_FILE, table.concat(history, "\n") .. "\n")
end

-- Lookup ----------------------------------------------------------------------

local function body_width()
  return gfx.width() - 2 * MARGIN
end

-- Wraps a definition into display lines, one blank line between paragraphs.
local function layout(definition)
  gfx.font(BODY_FONT)
  local width = body_width()
  local lines = {}
  local blank = true
  for paragraph in (definition .. "\n"):gmatch("(.-)\n") do
    paragraph = paragraph:gsub("^%s+", ""):gsub("%s+$", "")
    if paragraph == "" then
      if not blank then lines[#lines + 1] = "" end
      blank = true
    else
      for _, line in ipairs(gfx.wrap(paragraph, width)) do lines[#lines + 1] = line end
      blank = false
    end
  end
  if lines[#lines] == "" then lines[#lines] = nil end
  return lines
end

local function show_hit(index, definition)
  local hit = result.hits[index]
  if not definition then
    local err
    definition, err = dict.lookup(hit.dict.path, result.word)
    if not definition then definition = "Lookup failed: " .. tostring(err) end
  end
  result.index, result.top = index, 1
  result.lines = nil
  collectgarbage()
  result.lines = layout(definition)
end

local function run_search(word)
  local hits, first, failures = {}, nil, {}
  for _, d in ipairs(dicts) do
    local definition, headword = dict.lookup(d.path, word)
    d.needs_index = false
    if definition then
      hits[#hits + 1] = { dict = d, headword = headword }
      first = first or definition
    elseif headword ~= "not found" then
      failures[#failures + 1] = d.name .. ": " .. tostring(headword)
    end
  end
  remember(word)
  home_view.selected = 1
  if #hits == 0 then
    message = #failures > 0 and failures[1] or ('"' .. word .. '" was not found')
    return
  end
  result = { word = word, hits = hits }
  show_hit(1, first)
end

local function search(word)
  word = word and word:gsub("^%s+", ""):gsub("%s+$", "")
  if not word or word == "" then return end
  if #dicts == 0 then
    message = "No dictionaries installed"
    return
  end
  result = nil
  message = "Looking up..."
  for _, d in ipairs(dicts) do
    if d.needs_index then message = "Indexing dictionaries..." break end
  end
  pending = word
  sys.set_tick(250)
end

local function prompt(initial)
  sys.keyboard("Look up", initial or "", function(text)
    search(text)
    sys.redraw()
  end, MAX_WORD)
end

function on_tick()
  sys.set_tick(0)
  if not pending then return end
  local word = pending
  pending, message = nil, nil
  run_search(word)
  sys.redraw("half")
end

-- Drawing ---------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

local function draw_header(title, subtitle, right)
  local w = gfx.width()
  gfx.font("ui_large", "bold")
  gfx.text(MARGIN, MARGIN, fit(title, body_width()))
  local y = MARGIN + gfx.line_height() + 4
  gfx.font("ui")
  local right_w = right and gfx.text_width(right) or 0
  if right then gfx.text(w - MARGIN - right_w, y, right) end
  if subtitle then gfx.text(MARGIN, y, fit(subtitle, body_width() - right_w - 12)) end
  header_bottom = y + gfx.line_height() + 6
  gfx.line(MARGIN, header_bottom, w - MARGIN, header_bottom, true, 2)
end

local function draw_home_row(i, x, y, w, h, fg)
  gfx.font("ui", i == 1 and "bold" or "regular")
  local text = i == 1 and "Look up a word..." or history[i - 1]
  gfx.text(x + 12, y + (h - gfx.line_height()) // 2, fit(text, w - 24), fg)
end

local function draw_home(bottom)
  local count = #dicts
  draw_header("Dictionary", count == 1 and "1 dictionary" or (count .. " dictionaries"))
  local top = header_bottom + 8
  if message then
    gfx.font("ui")
    bottom = bottom - gfx.line_height()
    gfx.text(MARGIN, bottom, fit(message, body_width()))
    bottom = bottom - 8
  end
  if count == 0 then
    gfx.font("ui")
    for i, line in ipairs(gfx.wrap("No dictionaries found. Copy StarDict folders to /.dictionaries or /dictionaries.",
        body_width())) do
      gfx.text(MARGIN, top + (i - 1) * gfx.line_height(), line)
    end
    return
  end
  home_view:set_bounds(MARGIN, top, body_width(), bottom - top)
  home_view:draw(#history + 1, draw_home_row)
end

-- Matches the height draw_header() gives the header.
local function header_height()
  gfx.font("ui_large", "bold")
  local height = MARGIN + gfx.line_height() + 4
  gfx.font("ui")
  return height + gfx.line_height() + 6
end

local function draw_result(bottom)
  local hit = result.hits[result.index]
  local body_top = header_height() + 10
  gfx.font(BODY_FONT)
  local lh = gfx.line_height()
  page_lines = math.max(1, (bottom - body_top) // lh)
  local count = #result.lines
  local pages = math.max(1, (count + page_lines - 1) // page_lines)
  local page_no = (result.top - 1) // page_lines + 1

  local subtitle = hit.dict.name
  if #result.hits > 1 then subtitle = subtitle .. "  (" .. result.index .. "/" .. #result.hits .. ")" end
  draw_header(hit.headword, subtitle, pages > 1 and (page_no .. "/" .. pages) or nil)

  gfx.font(BODY_FONT)
  for i = result.top, math.min(count, result.top + page_lines - 1) do
    gfx.text(MARGIN, body_top + (i - result.top) * lh, result.lines[i])
  end
end

function draw()
  local more = result and #result.hits > 1
  local hints_height
  if result then
    hints_height = gfx.hints("Home", "Search", more and "Prev" or nil, more and "Next" or nil)
  else
    hints_height = gfx.hints("Exit", #dicts > 0 and "Look up" or nil, nil, nil)
  end
  local bottom = gfx.height() - hints_height - 8
  if result then draw_result(bottom) else draw_home(bottom) end
end

-- Input -----------------------------------------------------------------------

local function page(direction)
  local count = #result.lines
  local top = result.top + direction * page_lines
  if top < 1 or top > count then return end
  result.top = top
end

local function switch_hit(delta)
  local n = #result.hits
  if n < 2 then return end
  show_hit((result.index - 1 + delta) % n + 1)
end

local function activate_home(row)
  if #dicts == 0 then return end
  if row == 1 then prompt() else search(history[row - 1]) end
end

local function go_home()
  result = nil
  collectgarbage()
end

function on_button(button)
  if pending then return button == "back" end
  if button == "back" then
    if result then
      go_home()
    elseif message then
      message = nil
    else
      return false
    end
    sys.redraw()
    return true
  end

  if result then
    if button == "up" then page(-1)
    elseif button == "down" then page(1)
    elseif button == "left" then switch_hit(-1)
    elseif button == "right" then switch_hit(1)
    elseif button == "confirm" then prompt(result.word) end
  else
    message = nil
    if button == "up" then home_view:move(-1)
    elseif button == "down" then home_view:move(1)
    elseif button == "confirm" then activate_home(home_view.selected) end
  end
  sys.redraw()
end

function on_swipe(direction)
  if pending then return end
  if result then
    if direction == "up" then page(1)
    elseif direction == "down" then page(-1)
    elseif direction == "left" then switch_hit(1)
    elseif direction == "right" then switch_hit(-1) end
  else
    message = nil
    if direction == "up" then home_view:page(1)
    elseif direction == "down" then home_view:page(-1) end
  end
  sys.redraw()
end

function on_touch(x, y, kind)
  if pending then return end
  if result then
    if y < header_bottom then
      go_home()
    elseif kind == "long" then
      prompt(result.word)
    else
      page(x < gfx.width() // 2 and -1 or 1)
    end
  else
    message = nil
    local hit, row = home_view:tap(x, y)
    if hit == "row" then activate_home(row) end
  end
  sys.redraw()
end

discover()
load_history()
