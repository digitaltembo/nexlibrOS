-- title: Crossword
-- description: Puzzles!
--
-- Puzzles are Across Lite .puz files in the app's puzzles/ folder
-- (/apps/crossword/puzzles), browsed folder by folder. Filed as
-- <year>/<month>/[prefix]<year>-<month>-<day>[suffix].puz, the ones from four
-- years ago today head the list. Progress goes to saves/<file>.sav, the player
-- grid as one byte per square ("." black, "-" empty), so the .puz files are
-- never modified.
--
-- Grid: Up/Down/Left/Right move between white squares and set the direction;
-- Confirm types the current word on the keyboard (letters fill from the
-- word's first square; "." or a space leaves a square empty); Back opens the
-- menu, whose All clues lists every clue with its letters so far (Confirm
-- jumps to that word). Touch: tap a square to select it, tap it again or
-- long-press to switch direction, tap the clue to type, tap Clues for the
-- clue list.
--
-- Puzzles published without a solution (such as contest puzzles) or with a
-- scrambled one cannot be checked or revealed. Rebus squares take one letter.

local listview = require("listview")

local PUZZLE_DIR = "puzzles"
local SAVE_DIR = "saves"
local HEADER = 0x34
local MAGIC = "ACROSS&DOWN\0"
local MARGIN = 16
local GRID_MARGIN = 6
local CIRCLED = 0x80
local LETTER_FONTS = { "sans18", "sans16", "sans14", "sans12", "ui", "small" }

local dir = "" -- folder shown in the list, relative to PUZZLE_DIR
local entries = {} -- list rows: { kind = "heading" | "dir" | "puz", title, path, file, sub }
local opened = nil -- the list row of the open puzzle
local list_view = listview.new(56)
local p = nil -- the open puzzle, see parse() and open_puzzle()
local menu = nil -- { items = { { label, run } }, view } while the menu is open
local info = false -- showing the puzzle's title, author and notes
local clue_list = nil -- { items = word ids, view } while all clues are listed
local confirm_clear = false
local message = nil
local layout = nil -- grid geometry from the last draw, for touch

-- Files ------------------------------------------------------------------------

-- .puz text is ISO-8859-1; the fonts take UTF-8.
local function utf8_text(s)
  return (s:gsub("[\128-\255]", function(c) return utf8.char(c:byte()) end))
end

local function is_black(pz, i)
  return pz.solution:byte(i) == 46 -- "."
end

-- Numbers the squares the way the .puz clue list expects. Words are ids 1..n
-- in clue order. Per-square and per-word data is packed rather than kept in a
-- table per word or square, which matters on a 21x21 grid:
--   aw, dw    byte strings, one byte per square: the across / down word id
--             through it (0 for none)
--   words[id] start square + length * 1024 + clue number * 65536
--   clues[id] clue text; across, down: word ids in clue order
local MAX_SQUARES = 1023
local MAX_WORDS = 255

local function number_words(pz, clues)
  local w, h = pz.w, pz.h
  local aw, dw = {}, {}
  for i = 1, w * h do aw[i], dw[i] = 0, 0 end
  pz.words, pz.clues, pz.across, pz.down = {}, clues, {}, {}
  local n, id = 0, 0
  local function add(list, owner, start, step, across)
    id = id + 1
    list[#list + 1] = id
    local i, len = start, 0
    repeat
      owner[i] = id
      len = len + 1
      i = i + step
    until (across and (i - 1) % w == 0) or i > w * h or is_black(pz, i)
    pz.words[id] = start + len * 1024 + n * 65536
  end
  for r = 0, h - 1 do
    for c = 0, w - 1 do
      local i = r * w + c + 1
      if not is_black(pz, i) then
        local across = (c == 0 or is_black(pz, i - 1)) and c + 1 < w and not is_black(pz, i + 1)
        local down = (r == 0 or is_black(pz, i - w)) and r + 1 < h and not is_black(pz, i + w)
        if across or down then
          n = n + 1
          if across then add(pz.across, aw, i, 1, true) end
          if down then add(pz.down, dw, i, w, false) end
          if id > MAX_WORDS then return id end
        end
      end
    end
  end
  for i = 1, w * h do aw[i], dw[i] = string.char(aw[i]), string.char(dw[i]) end
  pz.aw, pz.dw = table.concat(aw), table.concat(dw)
  return id
end

-- Parses a .puz file's bytes; nil, err when malformed.
local function parse(data)
  if #data < HEADER or data:sub(3, 14) ~= MAGIC then return nil, "not an Across Lite .puz file" end
  local w, h = data:byte(0x2D), data:byte(0x2E)
  local clue_count, _, solution_state = string.unpack("<I2I2I2", data, 0x2F)
  local cells = w * h
  if cells == 0 or #data < HEADER + 2 * cells then return nil, "truncated grid" end
  if cells > MAX_SQUARES or clue_count > MAX_WORDS then return nil, "puzzle too large" end
  local pz = { w = w, h = h, solution = data:sub(HEADER + 1, HEADER + cells),
    player = data:sub(HEADER + cells + 1, HEADER + 2 * cells), checkable = solution_state == 0 }
  local pos = HEADER + 2 * cells + 1
  local strings = {}
  for i = 1, 4 + clue_count do -- title, author, copyright, clues, notes
    local e = data:find("\0", pos, true)
    if not e then
      if i < 4 + clue_count then return nil, "truncated clues" end
      break -- some files end without notes
    end
    strings[i] = utf8_text(data:sub(pos, e - 1))
    pos = e + 1
  end
  pz.title, pz.author, pz.copyright = strings[1], strings[2], strings[3]
  pz.notes = strings[4 + clue_count] or ""
  local clues = {}
  for i = 1, clue_count do clues[i] = strings[3 + i] end
  if number_words(pz, clues) ~= clue_count then return nil, "clue count does not match the grid" end
  -- Extra sections: <name:4><length:2><checksum:2><data><NUL>. GEXT marks circles.
  while pos + 8 <= #data do
    local name, len = data:sub(pos, pos + 3), string.unpack("<I2", data, pos + 4)
    if name == "GEXT" and len == cells then pz.markup = data:sub(pos + 8, pos + 7 + len) end
    pos = pos + 9 + len
  end
  return pz
end

local function save_path(file)
  return SAVE_DIR .. "/" .. file .. ".sav"
end

local function save()
  local ok, err = fs.write(save_path(p.file), p.grid)
  if not ok then message = "Save failed: " .. tostring(err) end
end

-- Puzzle list ------------------------------------------------------------------------
--
-- One folder at a time: subfolders, then .puz files. Titles and progress are
-- read only when a row is first drawn, so big archives open quickly. At the
-- top level, "On this day" lists puzzles dated four years ago today, found at
-- <year>/<month>/[prefix]<year>-<month>-<day>[suffix].puz.

local DAILY_YEARS_AGO = 4
local MONTHS = { "January", "February", "March", "April", "May", "June", "July", "August", "September",
  "October", "November", "December" }

local function is_puz(name)
  return name:sub(1, 1) ~= "." and name:lower():match("%.puz$") ~= nil
end

local function puz_entry(rel, name)
  return { kind = "puz", path = rel == "" and name or rel .. "/" .. name, file = name }
end

-- "10" inside a year folder reads as "October".
local function folder_label(rel, name)
  local m = tonumber(name:match("^%d%d?$") or "")
  if rel:match("^%d%d%d%d$") and m and MONTHS[m] then return MONTHS[m] end
  return name
end

-- Section heading and puzzles for today's date DAILY_YEARS_AGO years back.
local function daily_entries(out)
  local now = os.date("*t")
  if now.year < 2001 then return end -- clock not set yet
  local t = os.date("*t", os.time({ year = now.year - DAILY_YEARS_AGO, month = now.month, day = now.day, hour = 12 }))
  local rel = string.format("%04d/%02d", t.year, t.month)
  local date = string.format("%04d-%02d-%02d", t.year, t.month, t.day)
  local found = {}
  for _, name in ipairs(fs.list(PUZZLE_DIR .. "/" .. rel) or {}) do
    if is_puz(name) and name:find(date, 1, true) then found[#found + 1] = puz_entry(rel, name) end
  end
  if #found == 0 then return end
  -- The plain puzzle before its "-mini" and other variants.
  table.sort(found, function(a, b) return #a.file < #b.file or (#a.file == #b.file and a.file < b.file) end)
  out[#out + 1] = { kind = "heading", title = "On this day: " .. MONTHS[t.month] .. " " .. t.day .. ", " .. t.year }
  for _, e in ipairs(found) do out[#out + 1] = e end
  out[#out + 1] = { kind = "heading", title = "All puzzles" }
end

local function selectable(i)
  return entries[i] and entries[i].kind ~= "heading"
end

-- Lists folder rel ("" is the top level) and selects the row named select_name.
local function load_dir(rel, select_name)
  dir, entries = rel, {}
  if rel == "" then daily_entries(entries) end
  local folders, files = {}, {}
  for _, name in ipairs(fs.list(rel == "" and PUZZLE_DIR or PUZZLE_DIR .. "/" .. rel) or {}) do
    if name:sub(1, 1) ~= "." then
      if name:sub(-1) == "/" then
        name = name:sub(1, -2)
        folders[#folders + 1] = { kind = "dir", path = rel == "" and name or rel .. "/" .. name, file = name,
          title = folder_label(rel, name) }
      elseif is_puz(name) then
        files[#files + 1] = puz_entry(rel, name)
      end
    end
  end
  -- Years newest first at the top level; everything else in name order.
  local newest_first = rel == ""
  table.sort(folders, function(a, b)
    local na, nb = tonumber(a.file), tonumber(b.file)
    if na and nb then
      if newest_first then return na > nb end
      return na < nb
    end
    return a.file:lower() < b.file:lower()
  end)
  table.sort(files, function(a, b) return a.file:lower() < b.file:lower() end)
  for _, e in ipairs(folders) do entries[#entries + 1] = e end
  for _, e in ipairs(files) do entries[#entries + 1] = e end
  -- Drop a trailing "All puzzles" heading with nothing under it.
  if entries[#entries] and entries[#entries].kind == "heading" then entries[#entries] = nil end
  local pick = 1
  for i, e in ipairs(entries) do
    if select_name and e.kind ~= "heading" and e.file == select_name then
      pick = i
      break
    end
  end
  while entries[pick] and not selectable(pick) do pick = pick + 1 end
  list_view.selected, list_view.top, list_view.count = pick, 1, #entries
end

-- Fills in a puzzle row's title and size / progress line on first draw.
local function describe(entry)
  local path = PUZZLE_DIR .. "/" .. entry.path
  entry.title, entry.sub = (entry.file:gsub("%.[Pp][Uu][Zz]$", "")), "Unreadable"
  local head = fs.read(path, 0, HEADER) or ""
  if #head ~= HEADER or head:sub(3, 14) ~= MAGIC then return end
  local w, h = head:byte(0x2D), head:byte(0x2E)
  local cells = w * h
  local title = (fs.read(path, HEADER + 2 * cells, 160) or ""):match("^([^\0]*)")
  if title and title ~= "" then entry.title = utf8_text(title) end
  local saved = fs.read(save_path(entry.file))
  local grid = (saved and #saved == cells) and saved or fs.read(path, HEADER + cells, cells) or ""
  local _, blacks = grid:gsub("%.", "")
  local _, empty = grid:gsub("%-", "")
  local white = cells - blacks
  local pct = white > 0 and ((white - empty) * 100 // white) or 0
  entry.sub = w .. " × " .. h .. (pct > 0 and ("   " .. pct .. "% filled") or "")
end

-- Puzzle state -------------------------------------------------------------------

local function word_start(word) return p.words[word] % 1024 end
local function word_len(word) return p.words[word] // 1024 % 64 end
local function word_num(word) return p.words[word] // 65536 end
local function word_across(word) return p.aw:byte(word_start(word)) == word end

local function word_at(i, dir)
  local id = (dir == "A" and p.aw or p.dw):byte(i)
  if id == 0 then return nil end
  return id
end

local function current_word()
  return word_at(p.cur, p.dir)
end

-- The player's entry in square i: a letter, "-" (empty) or "." (black).
local function square(i)
  return p.grid:sub(i, i)
end

local function cell_of(word, k)
  return word_start(word) + k * (word_across(word) and 1 or p.w)
end

local function word_cells(word)
  local out = {}
  for k = 0, word_len(word) - 1 do out[#out + 1] = cell_of(word, k) end
  return out
end

-- The clue number printed in square i, if a word starts there.
local function number_at(i)
  local a, d = word_at(i, "A"), word_at(i, "D")
  if a and word_start(a) == i then return word_num(a) end
  if d and word_start(d) == i then return word_num(d) end
  return nil
end

local function all_cells()
  local out = {}
  for i = 1, p.w * p.h do
    if not is_black(p, i) then out[#out + 1] = i end
  end
  return out
end

-- Keeps the direction on a word that contains the cursor.
local function fix_dir()
  if not word_at(p.cur, p.dir) then p.dir = p.dir == "A" and "D" or "A" end
end

local function go_to_word(word)
  if not word then return end
  p.dir, p.cur = word_across(word) and "A" or "D", word_start(word)
  for k = 0, word_len(word) - 1 do
    local i = cell_of(word, k)
    if square(i) == "-" then
      p.cur = i
      return
    end
  end
end

local function next_word(word)
  local across = word_across(word)
  local list = across and p.across or p.down
  for k, id in ipairs(list) do
    if id == word then return list[k + 1] or (across and p.down or p.across)[1] or list[1] end
  end
  return list[1]
end

local function open_puzzle(entry)
  local data, err = fs.read(PUZZLE_DIR .. "/" .. entry.path)
  if not data then
    message = tostring(err)
    return
  end
  local pz, perr = parse(data)
  data = nil
  if not pz then
    message = entry.file .. ": " .. perr
    return
  end
  pz.file = entry.file -- saves are keyed by file name, wherever the puzzle sits
  local saved = fs.read(save_path(entry.file))
  local text = (saved and #saved == pz.w * pz.h) and saved or pz.player
  pz.player = nil
  local grid = {}
  for i = 1, pz.w * pz.h do
    local ch = text:sub(i, i)
    grid[i] = is_black(pz, i) and "." or ((ch == "." or ch == "") and "-" or ch)
  end
  pz.grid, pz.wrong = table.concat(grid), {}
  grid = nil
  p, opened = pz, entry
  p.cur, p.dir = 1, "A"
  go_to_word(p.across[1] or p.down[1])
  fix_dir()
end

local function close_puzzle()
  p, menu, info, clue_list, layout = nil, nil, false, nil, nil
  if opened then opened.sub = nil end -- reread its progress
  opened = nil
end

local function open_entry(entry)
  if entry.kind == "dir" then
    load_dir(entry.path)
  else
    open_puzzle(entry)
  end
end

local function check_complete()
  if p.grid:find("-", 1, true) then return end
  if not p.checkable then
    message = "All squares filled"
  elseif p.grid ~= p.solution then
    message = "All squares filled, but something is wrong"
  else
    message = "Solved!"
  end
end

local function set_cell(i, ch)
  if square(i) ~= ch then
    p.grid = p.grid:sub(1, i - 1) .. ch .. p.grid:sub(i + 1)
    p.wrong[i] = nil
  end
end

local function type_word()
  local word = current_word()
  if not word then return end
  local letters, any = {}, false
  local len = word_len(word)
  for k = 0, len - 1 do
    local ch = square(cell_of(word, k))
    letters[#letters + 1] = ch == "-" and "." or ch
    any = any or ch ~= "-"
  end
  local title = word_num(word) .. (word_across(word) and " Across" or " Down") .. " (" .. len .. ")"
  sys.keyboard(title, any and table.concat(letters) or "", function(text)
    if not text or not p then return end
    local k = 0
    for ch in text:upper():gmatch(".") do
      if k >= len then break end
      set_cell(cell_of(word, k), (ch == "." or ch == " ") and "-" or ch)
      k = k + 1
    end
    save()
    check_complete()
    if not message then go_to_word(next_word(word)) end
    sys.redraw()
  end, len + 8)
end

-- Moves to the next white square in direction (dc, dr) and takes its direction.
local function move(dc, dr)
  local r, c = (p.cur - 1) // p.w, (p.cur - 1) % p.w
  repeat
    r, c = r + dr, c + dc
  until r < 0 or r >= p.h or c < 0 or c >= p.w or not is_black(p, r * p.w + c + 1)
  if r >= 0 and r < p.h and c >= 0 and c < p.w then p.cur = r * p.w + c + 1 end
  p.dir = dc ~= 0 and "A" or "D"
  fix_dir()
end

local function check(cells)
  local wrong = 0
  for _, i in ipairs(cells) do
    local ch = square(i)
    if ch ~= "-" and ch ~= p.solution:sub(i, i) then
      p.wrong[i] = true
      wrong = wrong + 1
    end
  end
  message = wrong == 0 and "No mistakes" or (wrong .. (wrong == 1 and " wrong square" or " wrong squares"))
end

local function reveal(cells)
  for _, i in ipairs(cells) do set_cell(i, p.solution:sub(i, i)) end
  save()
  check_complete()
end

local function clear(cells)
  for _, i in ipairs(cells) do set_cell(i, "-") end
  save()
end

-- Menu -------------------------------------------------------------------------------

-- Every clue, across then down, with the current word selected.
local function open_clue_list()
  local items = {}
  for _, id in ipairs(p.across) do items[#items + 1] = id end
  for _, id in ipairs(p.down) do items[#items + 1] = id end
  clue_list = { items = items, view = listview.new(64) }
  local word = current_word()
  for k, id in ipairs(items) do
    if id == word then clue_list.view.selected = k end -- scrolled into view on draw
  end
end

local function open_menu()
  local word = current_word()
  local items = { { "Continue", function() end }, { "All clues", open_clue_list } }
  if p.checkable then
    items[#items + 1] = { "Check word", function() if word then check(word_cells(word)) end end }
    items[#items + 1] = { "Check puzzle", function() check(all_cells()) end }
    items[#items + 1] = { "Reveal square", function() reveal({ p.cur }) end }
    items[#items + 1] = { "Reveal word", function() if word then reveal(word_cells(word)) end end }
  end
  items[#items + 1] = { "Clear word", function() if word then clear(word_cells(word)) end end }
  items[#items + 1] = { "Puzzle info", function() info = true end }
  items[#items + 1] = { "Clear puzzle", function()
    if confirm_clear then
      clear(all_cells())
      p.wrong = {}
      go_to_word(p.across[1] or p.down[1])
      fix_dir()
    else
      confirm_clear = true
      message = "Select Clear puzzle again to erase every square"
      return true -- keep the menu open
    end
  end }
  items[#items + 1] = { "Puzzle list", close_puzzle }
  menu ={ items = items, view = listview.new(52) }
end

local function choose(item)
  local keep = item[2]()
  if not keep then
    menu = nil
    confirm_clear = false
  end
end

-- Drawing ------------------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

local function draw_header(title)
  local w = gfx.width()
  gfx.font("ui_large", "bold")
  gfx.text(MARGIN, MARGIN, fit(title, w - 2 * MARGIN))
  local bottom = MARGIN + gfx.line_height() + 8
  gfx.line(MARGIN, bottom, w - MARGIN, bottom, true, 2)
  return bottom
end

local function draw_lines(text, x, y, width, max_lines, bottom)
  for _, l in ipairs(gfx.wrap(text, width, max_lines)) do
    if y + gfx.line_height() > bottom then break end
    gfx.text(x, y, l)
    y = y + gfx.line_height()
  end
  return y
end

-- "Crossword", or the folder path with month names: "2022 / October".
local function list_title()
  if dir == "" then return "Crossword" end
  local parts, parent = {}, ""
  for name in dir:gmatch("[^/]+") do
    parts[#parts + 1] = folder_label(parent, name)
    parent = parent == "" and name or parent .. "/" .. name
  end
  return table.concat(parts, " / ")
end

local function draw_list(bottom)
  local top = draw_header(list_title())
  local w = gfx.width()
  if #entries == 0 then
    gfx.font("ui")
    local text = dir == "" and ("No puzzles yet. Copy Across Lite .puz files to " .. sys.app_dir() .. "/"
      .. PUZZLE_DIR .. " on the SD card, in folders if you like.") or "This folder has no puzzles."
    draw_lines(text, MARGIN, top + 24, w - 2 * MARGIN, 8, bottom)
    return
  end
  list_view:set_bounds(MARGIN, top + 8, w - 2 * MARGIN, bottom - top - 8)
  list_view:draw(#entries, function(i, x, y, rw, h, fg)
    local e = entries[i]
    if e.kind == "heading" then
      gfx.font("ui", "bold")
      local ty = y + h - gfx.line_height() - 10
      gfx.text(x + 4, ty, fit(e.title, rw - 8))
      gfx.line(x + 4, y + h - 6, x + rw - 4, y + h - 6)
      return
    end
    if e.kind == "puz" and not e.sub then describe(e) end
    gfx.font("ui", "bold")
    local lh = gfx.line_height()
    gfx.text(x + 12, y + 6, fit(e.title, rw - 24), fg)
    gfx.font("small")
    gfx.text(x + 12, y + 8 + lh, e.kind == "dir" and "Folder" or e.sub, fg)
  end)
end

local function letter_font(cell)
  for _, name in ipairs(LETTER_FONTS) do
    gfx.font(name, "bold")
    if gfx.line_height() <= cell * 0.72 then return name end
  end
  return "small"
end

local function draw_cell(i, x, y, cell, font, in_word)
  if is_black(p, i) then
    gfx.fill_rect(x, y, cell + 1, cell + 1)
    return
  end
  if i == p.cur then
    gfx.fill_rect(x, y, cell + 1, cell + 1, "light_gray")
    gfx.rect(x, y, cell + 1, cell + 1)
  else
    gfx.rect(x, y, cell + 1, cell + 1)
    if in_word then
      gfx.rect(x + 2, y + 2, cell - 3, cell - 3)
      gfx.rect(x + 3, y + 3, cell - 5, cell - 5)
    end
  end
  if p.markup and (p.markup:byte(i) & CIRCLED) ~= 0 then
    gfx.circle(x + cell // 2, y + cell // 2, cell // 2 - 2)
  end
  local ch = square(i)
  local number = ch == "-" and number_at(i) -- a letter hides the number
  if number then
    gfx.font("small")
    gfx.text(x + 2, y + 1, tostring(number))
  end
  if ch ~= "-" then
    gfx.font(font, "bold")
    local tw = gfx.text_width(ch)
    gfx.text(x + (cell - tw) // 2 + 1, y + cell - gfx.line_height() - cell // 12, ch)
  end
  if p.wrong[i] then gfx.line(x + 2, y + cell - 2, x + cell - 2, y + 2, true, 2) end
end

local function clue_label(word)
  return word_num(word) .. (word_across(word) and " Across" or " Down")
end

local function draw_clues(x, y, width, bottom)
  local word = current_word()
  if not word then return end
  gfx.font("ui", "bold")
  gfx.text(x, y, clue_label(word) .. " (" .. word_len(word) .. ")")
  y = y + gfx.line_height() + 2
  gfx.font("sans14")
  y = draw_lines(p.clues[word], x, y, width, 5, bottom) + 10
  local cross = word_at(p.cur, word_across(word) and "D" or "A")
  if cross then
    gfx.font("small")
    draw_lines(clue_label(cross) .. ": " .. p.clues[cross], x, y, width, 3, bottom)
  end
end

-- The word's letters so far, "_" for empty squares: "R A _ S E".
local function word_pattern(word)
  local out = {}
  for k = 0, word_len(word) - 1 do
    local ch = square(cell_of(word, k))
    out[#out + 1] = ch == "-" and "_" or ch
  end
  return table.concat(out, " ")
end

local function draw_clue_list(bottom)
  local top = draw_header("All clues")
  local w = gfx.width()
  gfx.font("ui")
  local lh = gfx.line_height()
  gfx.font("small")
  local view = clue_list.view
  view.row_h = 2 * lh + gfx.line_height() + 16
  gfx.font("ui", "bold")
  local label_w = gfx.text_width("000D") + 10
  view:set_bounds(MARGIN, top + 8, w - 2 * MARGIN, bottom - top - 8)
  view:draw(#clue_list.items, function(k, x, y, rw, _, fg)
    local word = clue_list.items[k]
    local tx = x + 10 + label_w
    gfx.font("ui", "bold")
    gfx.text(x + 10, y + 6, word_num(word) .. (word_across(word) and "A" or "D"), fg)
    gfx.font("ui")
    local ty = y + 6
    for _, l in ipairs(gfx.wrap(p.clues[word], rw - label_w - 20, 2)) do
      gfx.text(tx, ty, l, fg)
      ty = ty + lh
    end
    gfx.font("small")
    gfx.text(tx, y + 8 + 2 * lh, word_pattern(word), fg)
  end)
end

local function draw_puzzle(bottom)
  local sw = gfx.width()
  local top = GRID_MARGIN + 2
  local title_w = sw - 2 * GRID_MARGIN - 4
  local clues_x = nil
  if sys.has_touch() then
    -- Tappable button for the clue list; the rest of the title row opens the menu.
    gfx.font("ui", "bold")
    local bw = gfx.text_width("Clues") + 20
    clues_x = sw - GRID_MARGIN - bw
    gfx.round_rect(clues_x, top - 3, bw, gfx.line_height() + 6, 6)
    gfx.text(clues_x + 10, top, "Clues")
    title_w = clues_x - GRID_MARGIN - 12
  end
  gfx.font("ui", "bold")
  gfx.text(GRID_MARGIN + 2, top, fit(p.title ~= "" and p.title or p.file, title_w))
  top = top + gfx.line_height() + 6
  local landscape = sw > gfx.height()
  local cell
  if landscape then
    cell = math.min((bottom - top) // p.h, (sw * 3 // 5) // p.w)
  else
    -- Leave room under the grid for the clue.
    cell = math.min((sw - 2 * GRID_MARGIN) // p.w, (bottom - top - 150) // p.h)
  end
  cell = math.max(cell, 8)
  local gx = landscape and GRID_MARGIN or (sw - cell * p.w) // 2
  layout = { x = gx, y = top, cell = cell, clues_x = clues_x }
  local font = letter_font(cell)
  local word = current_word()
  local in_word = {}
  if word then
    for _, i in ipairs(word_cells(word)) do in_word[i] = true end
  end
  for r = 0, p.h - 1 do
    for c = 0, p.w - 1 do
      local i = r * p.w + c + 1
      draw_cell(i, gx + c * cell, top + r * cell, cell, font, in_word[i])
    end
  end
  local grid_right, grid_bottom = gx + cell * p.w, top + cell * p.h
  gfx.rect(gx - 1, top - 1, cell * p.w + 3, cell * p.h + 3)
  if landscape then
    layout.clue_y = top
    draw_clues(grid_right + 16, top, sw - grid_right - 16 - GRID_MARGIN, bottom)
  else
    layout.clue_y = grid_bottom + 8
    draw_clues(MARGIN, grid_bottom + 12, sw - 2 * MARGIN, bottom)
  end
end

local function draw_menu(bottom)
  local top = draw_header(p.title ~= "" and p.title or p.file)
  local w = gfx.width()
  menu.view:set_bounds(MARGIN, top + 8, w - 2 * MARGIN, bottom - top - 8)
  menu.view:draw(#menu.items, function(i, x, y, rw, h, fg)
    gfx.font("ui", "bold")
    gfx.text(x + 12, y + (h - gfx.line_height()) // 2, menu.items[i][1], fg)
  end)
end

local function draw_info(bottom)
  local top = draw_header(p.title ~= "" and p.title or p.file)
  local w = gfx.width()
  local y = top + 16
  gfx.font("ui", "bold")
  y = draw_lines(p.author, MARGIN, y, w - 2 * MARGIN, 2, bottom)
  gfx.font("small")
  y = draw_lines(p.copyright, MARGIN, y, w - 2 * MARGIN, 2, bottom) + 12
  gfx.font("ui")
  y = draw_lines(p.w .. " × " .. p.h .. ", " .. (#p.across + #p.down) .. " clues"
    .. (p.checkable and "" or ", no solution included"), MARGIN, y, w - 2 * MARGIN, 2, bottom) + 12
  if p.notes ~= "" then draw_lines(p.notes, MARGIN, y, w - 2 * MARGIN, 20, bottom) end
end

local function hint_labels()
  if not p then return dir == "" and "Exit" or "Up", "Open", nil, nil end
  if info then return "Back", nil, nil, nil end
  if menu then return "Back", "Select", nil, nil end
  if clue_list then return "Back", "Go to", "Prev", "Next" end
  return "Menu", "Type", "Left", "Right"
end

function draw()
  local w, h = gfx.width(), gfx.height()
  local bottom = h - gfx.hints(hint_labels()) - 8
  if message then
    gfx.font("ui")
    local lines = gfx.wrap(message, w - 2 * MARGIN, 3)
    bottom = bottom - #lines * gfx.line_height()
    draw_lines(message, MARGIN, bottom, w - 2 * MARGIN, 3, h)
    bottom = bottom - 8
  end
  if not p then
    draw_list(bottom)
  elseif info then
    draw_info(bottom)
  elseif menu then
    draw_menu(bottom)
  elseif clue_list then
    draw_clue_list(bottom)
  else
    draw_puzzle(bottom)
  end
end

-- Input ----------------------------------------------------------------------------------

local MOVES = { left = { -1, 0 }, right = { 1, 0 }, up = { 0, -1 }, down = { 0, 1 } }

local function handle_button(button)
  if not p then
    if button == "back" then
      if dir == "" then return false end
      local parent, name = dir:match("^(.*)/([^/]+)$")
      load_dir(parent or "", name or dir)
    elseif button == "up" or button == "left" or button == "down" or button == "right" then
      if #entries > 0 then
        local delta = (button == "up" or button == "left") and -1 or 1
        repeat list_view:move(delta) until selectable(list_view.selected)
      end
    elseif button == "confirm" and selectable(list_view.selected) then
      open_entry(entries[list_view.selected])
    end
  elseif info then
    if button == "back" or button == "confirm" then info = false end
  elseif menu then
    if button == "back" then
      menu, confirm_clear = nil, false
    elseif button == "up" or button == "left" then
      menu.view:move(-1)
    elseif button == "down" or button == "right" then
      menu.view:move(1)
    elseif button == "confirm" then
      choose(menu.items[menu.view.selected])
    end
  elseif clue_list then
    local view = clue_list.view
    if button == "back" then
      clue_list = nil
    elseif button == "up" then
      view:move(-1)
    elseif button == "down" then
      view:move(1)
    elseif button == "left" then
      view:page(-1)
    elseif button == "right" then
      view:page(1)
    elseif button == "confirm" then
      go_to_word(clue_list.items[view.selected])
      clue_list = nil
    end
  elseif button == "back" then
    open_menu()
  elseif button == "confirm" then
    type_word()
  elseif MOVES[button] then
    move(MOVES[button][1], MOVES[button][2])
  end
  return true
end

function on_button(button)
  message = nil
  local handled = handle_button(button)
  if handled then sys.redraw() end
  return handled
end

local function tap_grid(x, y, long)
  local l = layout
  if not l then return end
  local c, r = (x - l.x) // l.cell, (y - l.y) // l.cell
  if c >= 0 and c < p.w and r >= 0 and r < p.h then
    local i = r * p.w + c + 1
    if is_black(p, i) then return end
    if i == p.cur or long then
      p.cur = i
      p.dir = p.dir == "A" and "D" or "A"
    else
      p.cur = i
    end
    fix_dir()
  elseif y >= l.clue_y then
    type_word()
  end
end

function on_touch(x, y, kind)
  message = nil
  if not p then
    local hit, i = list_view:tap(x, y)
    if hit == "row" and selectable(i) then
      open_entry(entries[i])
    elseif not hit and dir ~= "" and y < list_view.y then
      on_button("back") -- tap on the header goes up a folder
      return
    end
  elseif info then
    info = false
  elseif menu then
    local hit, i = menu.view:tap(x, y)
    if hit == "row" then choose(menu.items[i]) end
  elseif clue_list then
    local hit, k = clue_list.view:tap(x, y)
    if hit == "row" then
      go_to_word(clue_list.items[k])
      clue_list = nil
    elseif not hit and y < clue_list.view.y then
      clue_list = nil -- tap on the header
    end
  elseif y < (layout and layout.y or 0) then
    if layout.clues_x and x >= layout.clues_x then open_clue_list() else open_menu() end
  else
    tap_grid(x, y, kind == "long")
  end
  sys.redraw()
end

fs.mkdir(PUZZLE_DIR)
fs.mkdir(SAVE_DIR)
load_dir("")
