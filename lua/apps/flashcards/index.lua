-- title: Flashcards
-- description: Simple Anki
--
-- Decks are Anki "Notes in Plain Text" exports (.txt, .tsv or .csv) in the
-- app's anki/ folder (/apps/flashcards/anki). The first two note fields are
-- the front and back; #separator:, #html: and "#<name> column:" export headers
-- are honoured. HTML is reduced to text and line breaks, [sound:] tags and
-- images are dropped, and cloze notes ({{c1::answer::hint}}) show "[hint]" /
-- "[...]" on the front.
--
-- Review: Confirm (or Down) shows the answer, then Left/Up = Again,
-- Confirm/Down = Good, Right = Easy. Touch: tap to show, tap a grade box.
--
-- Little is held in memory: per deck, the app's cache/ folder keeps
-- <deck>.idx (where each note starts; rebuilt when the deck's size changes),
-- <deck>.sched (4 bytes of schedule per note, updated in place) and
-- <deck>.meta (today's new-card count). Cards are identified by position, so
-- append new notes rather than reordering an exported deck.

local listview = require("listview")

local DECK_DIR = "anki"
local CACHE_DIR = "cache"
local CHUNK = 4096
local MAX_CARDS = 50000
local MAX_DUE = 500 -- due cards queued per session
local NEW_PER_DAY = 20
local MARGIN = 16
local GRADE_HEIGHT = 64
local UNSEEN = 0 -- schedule state of a card never reviewed
local INDEX_MAGIC = "FCX1"
local INDEX_HEADER = 12 -- magic, deck size, note count

local SEPARATORS = { tab = "\t", comma = ",", semicolon = ";", space = " ", pipe = "|", colon = ":" }
local GRADES = { "Again", "Good", "Easy" }

local decks = {}
local deck_view = listview.new(52)
local deck = nil -- the open deck, see open_deck
local card = nil -- { index, front, back } for the card being shown
local revealed = false
local message = nil
local header_bottom = 0
local grade_rects = {}

-- Scheduling ------------------------------------------------------------------

-- One integer per reviewed card: due day (15 bits), interval in days
-- (11 bits), ease in tenths (5 bits).
local function pack(due, ivl, ease)
  return due | (ivl << 15) | (ease << 26)
end

local function unpack_state(state)
  return state & 0x7FFF, (state >> 15) & 0x7FF, (state >> 26) & 0x1F
end

local function clock_day()
  local now = os.time()
  if now < 1704067200 then return nil end -- before 2024: clock never set
  local d = os.date("*t", now)
  return os.time({ year = d.year, month = d.month, day = d.day, hour = 12 }) // 86400
end

-- Simplified SM-2. Returns the new packed state and its interval.
local function next_state(state, grade, today)
  local ivl, ease = 0, 25
  if state ~= UNSEEN then
    local _
    _, ivl, ease = unpack_state(state)
  end
  if grade == 1 then
    ivl, ease = 0, math.max(13, ease - 2)
  elseif grade == 2 then
    if ivl == 0 then ivl = 1 elseif ivl == 1 then ivl = 3 else ivl = math.floor(ivl * ease / 10 + 0.5) end
  else
    ivl = ivl == 0 and 4 or math.floor(ivl * ease / 10 * 1.3 + 0.5)
    ease = math.min(31, ease + 1)
  end
  ivl = math.min(ivl, 2047)
  return pack(today + ivl, ivl, ease), ivl
end

local function format_interval(days)
  if days == 0 then return "<1d" end
  if days < 30 then return days .. "d" end
  if days < 365 then return (days // 30) .. "mo" end
  return string.format("%.1fy", days / 365)
end

-- Card text -------------------------------------------------------------------

-- Case-insensitive Lua pattern for a tag name.
local function ci(name)
  return (name:gsub("%a", function(c) return "[" .. c:lower() .. c:upper() .. "]" end))
end

local STYLE_BLOCK = "<" .. ci("style") .. ".-</" .. ci("style") .. "%s*>"
local SCRIPT_BLOCK = "<" .. ci("script") .. ".-</" .. ci("script") .. "%s*>"
local BREAK_TAGS = {}
for _, tag in ipairs({ "br", "div", "p", "li", "tr" }) do
  BREAK_TAGS[#BREAK_TAGS + 1] = "</?" .. ci(tag) .. "%f[^%w][^>]*>"
end
local ENTITIES = { nbsp = " ", amp = "&", lt = "<", gt = ">", quot = '"', apos = "'" }

local function entity(body)
  local code = tonumber(body:match("^#(%d+)$") or "") or tonumber(body:match("^#[xX](%x+)$") or "", 16)
  if code then return (code > 0 and code < 0x110000) and utf8.char(code) or "" end
  return ENTITIES[body:lower()] -- nil keeps the original text
end

local function strip_html(s)
  s = s:gsub(STYLE_BLOCK, ""):gsub(SCRIPT_BLOCK, "")
  for _, pattern in ipairs(BREAK_TAGS) do s = s:gsub(pattern, "\n") end
  s = s:gsub("<[^>]*>", ""):gsub("&(#?%w+);", entity)
  return s
end

local function tidy(s)
  s = s:gsub("%[sound:[^%]]*%]", ""):gsub("[ \t]+\n", "\n"):gsub("\n[ \t]+", "\n"):gsub("\n\n\n+", "\n\n")
  return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Splits one record on sep, honouring "quoted" fields with "" escapes.
local function split_fields(rec, sep)
  local fields, i, n = {}, 1, #rec
  while true do
    if rec:sub(i, i) == '"' then
      local parts, j = {}, i + 1
      while true do
        local q = rec:find('"', j, true)
        if not q then
          parts[#parts + 1] = rec:sub(j)
          i = n + 1
          break
        end
        parts[#parts + 1] = rec:sub(j, q - 1)
        if rec:sub(q + 1, q + 1) == '"' then
          parts[#parts + 1] = '"'
          j = q + 2
        else
          i = q + 1
          break
        end
      end
      fields[#fields + 1] = table.concat(parts)
      local s = rec:find(sep, i, true)
      if not s then break end
      i = s + 1
    else
      local s = rec:find(sep, i, true)
      if not s then
        fields[#fields + 1] = rec:sub(i)
        break
      end
      fields[#fields + 1] = rec:sub(i, s - 1)
      i = s + 1
    end
  end
  return fields
end

-- Byte range of a note: from its offset to the next note's (skipped blank
-- lines and line endings are trimmed by the caller).
local function note_range(d, index)
  local raw = fs.read(d.index_file, INDEX_HEADER + (index - 1) * 4, index < d.count and 8 or 4)
  if not raw or #raw < 4 then return nil end
  local start = string.unpack("<i4", raw)
  local stop = #raw >= 8 and string.unpack("<i4", raw, 5) or d.size
  return start, stop - start
end

local function read_state(d, index)
  local raw = fs.read(d.sched_file, (index - 1) * 4, 4)
  return raw and #raw == 4 and string.unpack("<i4", raw) or UNSEEN
end

local function load_card(d, index)
  local start, len = note_range(d, index)
  local rec = ((start and fs.read(d.path, start, len)) or ""):gsub("[\r\n]+$", "")
  local kept = {}
  for col, field in ipairs(split_fields(rec, d.sep)) do
    if not d.skip[col] then kept[#kept + 1] = field end
  end
  local front, back = kept[1] or "", kept[2] or ""
  if d.html then front, back = strip_html(front), strip_html(back) end
  if front:find("{{c%d+::") then
    local question = front:gsub("{{c%d+::(.-)}}", function(inner)
      local _, hint = inner:match("^(.-)::(.*)$")
      return "[" .. (hint or "...") .. "]"
    end)
    local answer = front:gsub("{{c%d+::(.-)}}", function(inner) return inner:match("^(.-)::") or inner end)
    front, back = question, back ~= "" and (answer .. "\n\n" .. back) or answer
  end
  front, back = tidy(front), tidy(back)
  return { index = index, front = front ~= "" and front or "(empty)", back = back, state = read_state(d, index) }
end

-- Decks -----------------------------------------------------------------------

local function load_deck_list()
  decks = {}
  for _, name in ipairs(fs.list(DECK_DIR) or {}) do
    local base, ext = name:match("^(.+)%.(%w+)$")
    ext = ext and ext:lower()
    if base and name:sub(1, 1) ~= "." and (ext == "txt" or ext == "tsv" or ext == "csv") then
      decks[#decks + 1] = { name = base, path = DECK_DIR .. "/" .. name, csv = ext == "csv" }
    end
  end
  table.sort(decks, function(a, b) return a.name:lower() < b.name:lower() end)
end

-- Reads the export headers; returns the byte offset where the notes start.
local function read_headers(d)
  local head = fs.read(d.path, 0, CHUNK)
  if not head then return nil end
  local pos = head:sub(1, 3) == "\239\187\191" and 4 or 1 -- UTF-8 BOM
  while head:sub(pos, pos) == "#" do
    local nl = head:find("\n", pos, true)
    if not nl then break end
    local key, value = head:sub(pos + 1, nl - 1):gsub("\r$", ""):match("^([%w ]+):(.*)$")
    if key == "separator" then
      d.sep = SEPARATORS[value:lower()] or (#value == 1 and value) or d.sep
    elseif key == "html" then
      d.html = value ~= "false"
    elseif key and key:match(" column$") then
      d.skip[tonumber(value) or 0] = true
    end
    pos = nl + 1
  end
  return pos - 1
end

-- Uses the index file when it was built from a deck of the current size.
local function load_index(d)
  local head = fs.read(d.index_file, 0, INDEX_HEADER)
  if not head or #head < INDEX_HEADER or head:sub(1, 4) ~= INDEX_MAGIC then return false end
  local size, count = string.unpack("<i4i4", head, 5)
  if size ~= d.size or fs.size(d.index_file) ~= INDEX_HEADER + 4 * count then return false end
  d.count = count
  return true
end

-- Writes the index file: the start offset of every note. Quoting follows
-- CSV: a quote opens a field only at the field's start, "" inside a quoted
-- field is a literal quote, and a newline inside a quoted field does not end
-- the note. The header is written last, so an interrupted build is redone.
local function build_index(d, start)
  if not fs.write(d.index_file, INDEX_MAGIC .. string.pack("<i4i4", -1, 0)) then return false end
  local sep_byte = d.sep:byte()
  local offset, rec_start, in_quote, prev = start, start, false, nil
  local batch = {}
  local function flush()
    if #batch > 0 then fs.append(d.index_file, table.concat(batch)) end
    batch = {}
  end
  local function add(len)
    if len > 1 and d.count < MAX_CARDS then
      d.count = d.count + 1
      batch[#batch + 1] = string.pack("<i4", rec_start)
      if #batch == 256 then flush() end
    end
  end
  while offset < d.size and d.count < MAX_CARDS do
    -- One byte of lookahead, so a "" escape split across chunks is seen whole.
    local chunk = fs.read(d.path, offset, CHUNK + 1)
    if not chunk or chunk == "" then break end
    local limit = math.min(#chunk, CHUNK)
    local i = 1
    while true do
      local s = chunk:find('[\n"]', i)
      if not s or s > limit then break end
      local pos = offset + s - 1
      if chunk:byte(s) == 34 then
        if not in_quote then
          local before = s > 1 and chunk:byte(s - 1) or prev
          in_quote = pos == rec_start or before == sep_byte
        elseif chunk:byte(s + 1) == 34 then
          s = s + 1 -- escaped quote
        else
          in_quote = false
        end
      elseif not in_quote then
        add(pos - rec_start)
        rec_start = pos + 1
      end
      i = s + 1
    end
    local consumed = math.max(limit, i - 1)
    prev = chunk:byte(consumed)
    offset = offset + consumed
  end
  if rec_start < d.size then add(d.size - rec_start) end
  flush()
  if d.count >= MAX_CARDS then message = "Only the first " .. MAX_CARDS .. " notes are used" end
  return fs.write(d.index_file, INDEX_MAGIC .. string.pack("<i4i4", d.size, d.count), 0)
end

-- Grows the schedule file to one zeroed (UNSEEN) slot per note.
local function ensure_schedule(d)
  local have = (fs.size(d.sched_file) or 0) // 4
  local zeros = string.rep("\0", CHUNK)
  while have < d.count do
    local n = math.min(d.count - have, CHUNK // 4)
    if not fs.append(d.sched_file, zeros:sub(1, n * 4)) then return false end
    have = have + n
  end
  return true
end

local function save_meta(d)
  fs.write(d.meta_file, d.today .. " " .. d.new_today)
end

local function load_meta(d)
  local meta_day, meta_new = (fs.read(d.meta_file) or ""):match("^(%d+) (%d+)")
  meta_day, meta_new = tonumber(meta_day), tonumber(meta_new)
  local today = clock_day()
  if not today then
    today = (meta_day or 0) + 1
    message = "Clock not set: each session counts as a new day"
  end
  d.today = today
  d.new_today = meta_day == today and meta_new or 0
  save_meta(d)
end

-- Streams the schedule file: due cards first, then today's new cards.
local function build_queue(d)
  local due, new = {}, {}
  local new_slots = NEW_PER_DAY - d.new_today
  for base = 0, d.count - 1, CHUNK // 4 do
    local raw = fs.read(d.sched_file, base * 4, math.min(CHUNK // 4, d.count - base) * 4) or ""
    for j = 0, #raw // 4 - 1 do
      local state = string.unpack("<i4", raw, j * 4 + 1)
      if state == UNSEEN then
        if #new < new_slots then new[#new + 1] = base + j + 1 end
      elseif (state & 0x7FFF) <= d.today and #due < MAX_DUE then
        due[#due + 1] = base + j + 1
      end
    end
  end
  for _, i in ipairs(new) do due[#due + 1] = i end
  d.queue, d.qpos = due, 1
end

local function open_deck(entry)
  local d = { name = entry.name, path = entry.path, sep = entry.csv and "," or "\t", html = true, skip = {},
    size = fs.size(entry.path) or 0, count = 0 }
  local cache = CACHE_DIR .. "/" .. entry.name
  d.index_file, d.sched_file, d.meta_file = cache .. ".idx", cache .. ".sched", cache .. ".meta"
  local start = read_headers(d)
  if not start then
    message = "Cannot read " .. entry.path
    return
  end
  if not load_index(d) and not build_index(d, start) then
    message = "Cannot write the deck index"
    return
  end
  if d.count == 0 then
    message = "No notes found in " .. entry.name
    return
  end
  if not ensure_schedule(d) then
    message = "Cannot write progress"
    return
  end
  load_meta(d)
  build_queue(d)
  deck, card, revealed = d, nil, false
end

local function close_deck()
  deck, card, revealed = nil, nil, false
end

local function current_card()
  local index = deck.queue[deck.qpos]
  if not index then return nil end
  if not card or card.index ~= index then card = load_card(deck, index) end
  return card
end

local function grade(g)
  local d, c = deck, current_card()
  if not c then return end
  local state = next_state(c.state, g, d.today)
  fs.write(d.sched_file, string.pack("<i4", state), (c.index - 1) * 4)
  if c.state == UNSEEN then
    d.new_today = d.new_today + 1
    save_meta(d)
  end
  c.state = state
  if g == 1 then d.queue[#d.queue + 1] = c.index end -- see it again this session
  d.qpos = d.qpos + 1
  revealed = false
end

-- Drawing ---------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

-- Wraps each paragraph of text, up to max_lines lines in total.
local function wrap_text(text, width, max_lines)
  local out = {}
  for para in (text .. "\n"):gmatch("(.-)\n") do
    if #out >= max_lines then break end
    if para == "" then
      out[#out + 1] = ""
    else
      for _, l in ipairs(gfx.wrap(para, width, max_lines - #out)) do out[#out + 1] = l end
    end
  end
  return out
end

local function draw_lines(text_lines, x, y, lh)
  for _, l in ipairs(text_lines) do
    gfx.text(x, y, l)
    y = y + lh
  end
  return y
end

local function centered(text, x, y, w)
  gfx.text(x + (w - gfx.text_width(text)) // 2, y, text)
end

local function draw_header(title, right)
  local w = gfx.width()
  gfx.font("ui_large", "bold")
  local right_w = 0
  if right then
    gfx.font("ui")
    right_w = gfx.text_width(right) + 12
    gfx.text(w - MARGIN - right_w + 12, MARGIN + 4, right)
    gfx.font("ui_large", "bold")
  end
  gfx.text(MARGIN, MARGIN, fit(title, w - 2 * MARGIN - right_w))
  header_bottom = MARGIN + gfx.line_height() + 8
  gfx.line(MARGIN, header_bottom, w - MARGIN, header_bottom, true, 2)
end

local function draw_grades(y, w)
  local bw = (w - 2 * MARGIN - 16) // 3
  grade_rects = {}
  for g = 1, 3 do
    local bx = MARGIN + (g - 1) * (bw + 8)
    gfx.round_rect(bx, y, bw, GRADE_HEIGHT, 8)
    gfx.round_rect(bx + 1, y + 1, bw - 2, GRADE_HEIGHT - 2, 7)
    gfx.font("ui", "bold")
    local lh = gfx.line_height()
    centered(GRADES[g], bx, y + 8, bw)
    local _, ivl = next_state(card.state, g, deck.today)
    gfx.font("ui")
    centered(format_interval(ivl), bx, y + 12 + lh, bw)
    grade_rects[g] = { bx, y, bw, GRADE_HEIGHT }
  end
end

local function draw_review(w, bottom)
  local c = current_card()
  local top = header_bottom + 16
  local text_bottom = revealed and bottom - GRADE_HEIGHT - 16 or bottom
  gfx.font("sans18")
  local lh = gfx.line_height()
  local max_lines = math.max(1, (text_bottom - top) // lh)
  local y = draw_lines(wrap_text(c.front, w - 2 * MARGIN, revealed and math.max(1, max_lines // 2) or max_lines),
    MARGIN, top, lh)
  if revealed then
    y = y + 8
    gfx.line(MARGIN, y, w - MARGIN, y, true, 2)
    y = y + 12
    gfx.font("sans16")
    local lh2 = gfx.line_height()
    local remaining = (text_bottom - y) // lh2
    if remaining > 0 and c.back ~= "" then draw_lines(wrap_text(c.back, w - 2 * MARGIN, remaining), MARGIN, y, lh2) end
    draw_grades(bottom - GRADE_HEIGHT, w)
  end
end

local function draw_deck_row(i, x, y, w, h, fg)
  gfx.font("ui", "bold")
  gfx.text(x + 12, y + (h - gfx.line_height()) // 2, fit(decks[i].name, w - 24), fg)
end

local function hint_labels()
  if not deck then return "Exit", #decks > 0 and "Open" or nil, nil, nil end
  if deck.qpos > #deck.queue then return "Decks", "Close", nil, nil end
  if not revealed then return "Decks", "Show", nil, nil end
  return "Decks", "Good", "Again", "Easy"
end

function draw()
  local w, h = gfx.width(), gfx.height()
  local bottom = h - gfx.hints(hint_labels()) - 8
  if message then
    gfx.font("ui")
    bottom = bottom - gfx.line_height()
    gfx.text(MARGIN, bottom, fit(message, w - 2 * MARGIN))
    bottom = bottom - 8
  end

  if not deck then
    draw_header("Flashcards")
    if #decks == 0 then
      gfx.font("ui")
      local y = header_bottom + 24
      for _, l in ipairs(wrap_text("No decks yet. Export notes from Anki as \"Notes in Plain Text\" and copy the "
        .. ".txt file to " .. sys.app_dir() .. "/" .. DECK_DIR .. " on the SD card.", w - 2 * MARGIN, 8)) do
        gfx.text(MARGIN, y, l)
        y = y + gfx.line_height()
      end
      return
    end
    deck_view:set_bounds(MARGIN, header_bottom + 8, w - 2 * MARGIN, bottom - header_bottom - 8)
    deck_view:draw(#decks, draw_deck_row)
    return
  end

  local left = #deck.queue - deck.qpos + 1
  draw_header(deck.name, left > 0 and (left .. " left") or nil)
  if left <= 0 then
    gfx.font("sans18", "bold")
    centered("All done for today", 0, (header_bottom + bottom) // 2 - gfx.line_height(), w)
    return
  end
  draw_review(w, bottom)
end

-- Input -----------------------------------------------------------------------

local function in_rect(r, x, y)
  return x >= r[1] and x < r[1] + r[3] and y >= r[2] and y < r[2] + r[4]
end

function on_button(button)
  if button == "back" then
    if message then
      message = nil
    elseif deck then
      close_deck()
    else
      return false
    end
    sys.redraw()
    return true
  end
  message = nil
  if not deck then
    if button == "up" then
      deck_view:move(-1)
    elseif button == "down" then
      deck_view:move(1)
    elseif button == "confirm" and decks[deck_view.selected] then
      open_deck(decks[deck_view.selected])
    end
  elseif deck.qpos > #deck.queue then
    if button == "confirm" then close_deck() end
  elseif not revealed then
    if button == "confirm" or button == "down" then revealed = true end
  elseif button == "left" or button == "up" then
    grade(1)
  elseif button == "confirm" or button == "down" then
    grade(2)
  elseif button == "right" then
    grade(3)
  end
  sys.redraw()
end

function on_touch(x, y)
  message = nil
  if not deck then
    local hit, row = deck_view:tap(x, y)
    if hit == "row" then open_deck(decks[row]) end
  elseif y < header_bottom or deck.qpos > #deck.queue then
    close_deck()
  elseif not revealed then
    revealed = true
  else
    for g, r in ipairs(grade_rects) do
      if in_rect(r, x, y) then grade(g) break end
    end
  end
  sys.redraw()
end

fs.mkdir(DECK_DIR)
fs.mkdir(CACHE_DIR)
load_deck_list()
