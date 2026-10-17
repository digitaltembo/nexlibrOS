-- title: Wikipedia
-- description: Offline Wikipedia articles
--
-- Reads a pack built by scripts/wikipedia/build_wiki_pack.py from the app's
-- data/ directory (/apps/wikipedia/data on the SD card).
--
-- Home: Search, Random article, then recently read articles.
-- Results: Up/Down select, Confirm opens, Back returns home.
-- Article: Down/Right next page, Up/Left previous page, Confirm picks a link on
-- the page (then Left/Right/Up/Down move, Confirm follows, Back cancels).
-- Back returns to the article a link came from, else the results (or home).
-- Touch: tap a link to follow it, tap the right/left half (or swipe) to turn
-- pages, tap the title to go back, long-press to search.

local listview = require("listview")
local wikipack = require("wikipack")
local links = require("links")

local PACK_DIR = "data" -- relative to the app directory, so no permission is needed
local HISTORY_FILE = "history.txt"
local MAX_HISTORY = 20
local MAX_QUERY = 80
local RESULTS = 30
local MARGIN = 16
local ROW_HEIGHT = 52
local CHUNK = 6 * 1024     -- article bytes fetched per read
local MAX_CHUNK = 16 * 1024 -- largest read, for a paragraph longer than CHUNK
local MAX_TRAIL = 32        -- followed links remembered for Back

-- Article text uses the reading font, size and line spacing from the reader settings.
local BODY = { "reader", "regular" }
local HEADING = { "reader", "bold" }
local STYLES = {
  heading2 = HEADING,
  heading3 = HEADING,
  heading4 = HEADING,
  item = BODY,
  infobox = BODY,
  note = { "reader", "italic" },
  para = BODY,
}

local pack, open_error = wikipack.open(PACK_DIR)
local history = {}  -- { id, title }, newest first
local home_view = listview.new(ROW_HEIGHT)
local results = nil -- { query, hits, view }
local article = nil -- { id, title, size, pos, lines, next_pos, back, links, sel }
local trail = {}    -- articles left by following a link: { id, pos (packed), back }
-- back: earlier page positions packed as off * SUB_LIMIT + sub, one integer per page.
local SUB_LIMIT = 1024
local message = nil
local header_bottom = 0
local hints_height = 0 -- the hint bar's height, from the previous draw

-- History ---------------------------------------------------------------------

local function load_history()
  history = {}
  for id, title in (fs.read(HISTORY_FILE) or ""):gmatch("(%d+)\t([^\n]*)\n") do
    if #history < MAX_HISTORY then history[#history + 1] = { id = tonumber(id), title = title } end
  end
end

local function remember(id, title)
  for i = #history, 1, -1 do
    if history[i].id == id then table.remove(history, i) end
  end
  table.insert(history, 1, { id = id, title = title })
  while #history > MAX_HISTORY do table.remove(history) end
  local out = {}
  for i, h in ipairs(history) do out[i] = h.id .. "\t" .. h.title .. "\n" end
  fs.write(HISTORY_FILE, table.concat(out))
end

-- Article text -----------------------------------------------------------------

local function body_width()
  return gfx.width() - 2 * MARGIN
end

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

-- One cached window of article bytes; paragraphs are read from it by offset.
local cache = { id = nil, start = 0, data = "" }

-- Returns the paragraph starting at byte offset off and the offset after it,
-- or nil at the end of the article.
local function paragraph(off)
  local size = article.size
  if off >= size then return nil end
  local rel = off - cache.start
  local cached = cache.id == article.id and rel >= 0 and rel < #cache.data
  local stop = cached and cache.data:find("\n", rel + 1, true)
  -- Without a newline the cached paragraph is complete only if the cache reaches the end.
  if not stop and not (cached and cache.start + #cache.data >= size) then
    cache.data = "" -- release before reading the next window
    local data, err
    for _, len in ipairs({ CHUNK, MAX_CHUNK }) do
      data, err = pack:read(article.id, off, len)
      if not data then error("read failed: " .. tostring(err)) end
      if data:find("\n", 1, true) or off + #data >= size then break end
    end
    cache.id, cache.start, cache.data = article.id, off, data
    rel = 0
    stop = data:find("\n", 1, true)
  end
  if stop then return cache.data:sub(rel + 1, stop - 1), cache.start + stop end
  local text = cache.data:sub(rel + 1)
  return text, cache.start + #cache.data -- last paragraph, or one cut at MAX_CHUNK
end

-- Wrapped display lines for one paragraph: { text, style, x, gap } where gap
-- is extra space above the line.
local function wrap(text)
  local kind, body = "para", text
  local lead = text:byte(1)
  if lead == 1 then
    kind, body = "heading" .. math.min(4, math.max(2, tonumber(text:sub(2, 2)) or 2)), text:sub(3)
  elseif lead == 2 then
    kind, body = "item", text:sub(2)
  elseif lead == 3 then
    local k, v = text:sub(2):match("^([^\31]*)\31(.*)$")
    kind, body = "infobox", (k or "") .. ": " .. (v or "")
  elseif lead == 5 then
    kind, body = "note", "[Article truncated]"
  end
  local style = STYLES[kind]
  gfx.font(style[1], style[2])
  local indent = kind == "item" and 24 or 0
  local plain, spans = links.parse(body)
  local lines = {}
  for i, line in ipairs(gfx.wrap(plain, body_width() - indent)) do
    lines[i] = { text = line, style = style, x = MARGIN + indent, gap = 0, bullet = kind == "item" and i == 1 }
  end
  if spans then links.attach(lines, plain, spans) end
  if lines[1] then
    lines[1].gap = kind:sub(1, 7) == "heading" and 14 or (kind == "infobox" and 0 or 8)
  end
  return lines
end

local function line_height(style)
  gfx.font(style[1], style[2])
  return gfx.line_height()
end

-- Lays out the page starting at pos = { off, sub } (sub: wrapped lines of that
-- paragraph already shown). Returns the page lines and the next page's pos.
local function layout(pos, top, bottom)
  local lines = {}
  local y = top
  local off, sub = pos.off, pos.sub
  while true do
    local text, next_off = paragraph(off)
    if not text then return lines, nil end
    local wrapped = wrap(text)
    for i = sub + 1, #wrapped do
      local line = wrapped[i]
      local gap = (#lines == 0) and 0 or line.gap
      local h = line_height(line.style)
      if y + gap + h > bottom then
        if #lines == 0 then return { line }, { off = off, sub = i } end -- taller than the page
        return lines, { off = off, sub = i - 1 }
      end
      y = y + gap
      line.y = y
      lines[#lines + 1] = line
      y = y + h
    end
    off, sub = next_off, 0
  end
end

-- Screen layout ----------------------------------------------------------------

local function header_height()
  gfx.font("ui_large", "bold")
  local height = MARGIN + gfx.line_height() + 4
  gfx.font("ui")
  return height + gfx.line_height() + 6
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

local function body_bounds(bottom)
  return header_height() + 10, bottom
end

-- Navigation -------------------------------------------------------------------

local function pack_pos(pos)
  return pos.off * SUB_LIMIT + math.min(pos.sub, SUB_LIMIT - 1)
end

local function unpack_pos(packed)
  return { off = packed // SUB_LIMIT, sub = packed % SUB_LIMIT }
end

local function open_article(id)
  article = nil
  collectgarbage()
  article = { id = id, title = pack:title(id), size = pack:size(id), pos = { off = 0, sub = 0 }, back = {} }
  remember(id, article.title)
end

local function close_article()
  article = nil
  trail = {}
  cache.id, cache.data = nil, ""
  collectgarbage()
end

local function follow(id)
  trail[#trail + 1] = { id = article.id, pos = pack_pos(article.pos), back = article.back }
  if #trail > MAX_TRAIL then table.remove(trail, 1) end
  open_article(id)
end

-- Returns to the article the current one was reached from; false if none.
local function retrace()
  local prev = table.remove(trail)
  if not prev then return false end
  open_article(prev.id)
  article.pos, article.back = unpack_pos(prev.pos), prev.back
  return true
end

local function turn(direction)
  if article.failed then
    article.failed, article.lines = nil, nil
    return
  end
  if direction > 0 then
    if not article.next_pos then return end
    article.back[#article.back + 1] = pack_pos(article.pos)
    article.pos = article.next_pos
  else
    if #article.back == 0 then return end
    article.pos = unpack_pos(table.remove(article.back))
  end
  article.lines, article.links, article.sel = nil, nil, nil
end

local function search(query)
  query = query and query:gsub("^%s+", ""):gsub("%s+$", "")
  if not query or query == "" then return end
  local hits = pack:search(query, RESULTS)
  if #hits == 0 then
    message = '"' .. query .. '" not found'
    return
  end
  results = { query = query, hits = hits, view = listview.new(ROW_HEIGHT) }
  close_article()
  if #hits == 1 then open_article(hits[1].id) end
end

local function prompt(initial)
  sys.keyboard("Search Wikipedia", initial or "", function(text)
    message = nil
    search(text)
    sys.redraw("half")
  end, MAX_QUERY)
end

-- Drawing ----------------------------------------------------------------------

local function draw_lines(lines, sel)
  for _, line in ipairs(lines) do
    gfx.font(line.style[1], line.style[2])
    if line.bullet then gfx.text(line.x - 18, line.y, "•") end
    gfx.text(line.x, line.y, line.text)
    local lh = gfx.line_height()
    for _, seg in ipairs(line.links or {}) do
      if seg.n == sel then
        gfx.fill_rect(seg.x0 - 2, line.y, seg.x1 - seg.x0 + 4, lh)
        gfx.text(seg.x0, line.y, seg.text, false)
      else
        gfx.line(seg.x0, line.y + lh - 2, seg.x1, line.y + lh - 2)
      end
    end
  end
end

-- The link under a tap, as an article id.
local function link_at(x, y)
  for _, line in ipairs(article.lines or {}) do
    local lh = line_height(line.style)
    if y >= line.y - 4 and y < line.y + lh + 4 then
      for _, seg in ipairs(line.links or {}) do
        if x >= seg.x0 - 6 and x <= seg.x1 + 6 then return seg.id end
      end
    end
  end
end

-- Lays out the current page; runs before the hints, whose labels depend on its links.
local function layout_article()
  if article.lines then return end
  local top, bottom = body_bounds(gfx.height() - hints_height - 8)
  local ok, lines, next_pos = pcall(layout, article.pos, top, bottom)
  if ok then
    article.lines, article.next_pos = lines, next_pos
    article.links = links.on_page(lines)
    if article.size == 0 then
      gfx.font("ui")
      article.lines = { { text = "This article has no text.", style = { "ui", "regular" }, x = MARGIN, y = top } }
    end
  else
    -- Typically "not enough memory" from fs.inflate; turning the page retries.
    cache.id, cache.data = nil, ""
    article.lines, article.next_pos = {}, nil
    gfx.font("ui")
    for i, line in ipairs(gfx.wrap("Could not read this page: " .. tostring(lines), body_width())) do
      article.lines[i] = { text = line, style = { "ui", "regular" }, x = MARGIN, y = top + (i - 1) * gfx.line_height() }
    end
    article.failed = true
  end
end

local function draw_article()
  local pct = article.size > 0 and math.floor(100 * article.pos.off / article.size) or 100
  draw_header(article.title, nil, pct .. "%")
  draw_lines(article.lines, article.sel)
end

local function draw_list(view, count, top, bottom, row_text)
  view:set_bounds(MARGIN, top, body_width(), bottom - top)
  view:draw(count, function(i, x, y, w, h, fg)
    local text, bold, sub = row_text(i)
    gfx.font("ui", bold and "bold" or "regular")
    if sub then
      local lh = gfx.line_height()
      gfx.text(x + 12, y + (h - 2 * lh) // 2, fit(text, w - 24), fg)
      gfx.font("small")
      gfx.text(x + 12, y + (h - 2 * lh) // 2 + lh, fit(sub, w - 24), fg)
    else
      gfx.text(x + 12, y + (h - gfx.line_height()) // 2, fit(text, w - 24), fg)
    end
  end)
end

local function draw_results(bottom)
  draw_header("Results", '"' .. results.query .. '"', #results.hits .. "")
  draw_list(results.view, #results.hits, header_bottom + 8, bottom, function(i)
    local hit = results.hits[i]
    return hit.title, false, hit.via and ("from " .. hit.via) or nil
  end)
end

local function draw_home(bottom)
  local subtitle = pack and (pack.hdr.nonempty or pack:count()) .. " articles" or nil
  draw_header("Wikipedia", subtitle)
  local top = header_bottom + 8
  if message then
    gfx.font("ui")
    bottom = bottom - gfx.line_height()
    gfx.text(MARGIN, bottom, fit(message, body_width()))
    bottom = bottom - 8
  end
  if not pack then
    gfx.font("ui")
    local text = "No Wikipedia pack found in /apps/wikipedia/" .. PACK_DIR .. " (" .. tostring(open_error) .. "). Build one with "
        .. "scripts/wikipedia/build_wiki_pack.py and copy it there."
    for i, line in ipairs(gfx.wrap(text, body_width())) do
      gfx.text(MARGIN, top + (i - 1) * gfx.line_height(), line)
    end
    return
  end
  draw_list(home_view, #history + 2, top, bottom, function(i)
    if i == 1 then return "Search...", true end
    if i == 2 then return "Random article", true end
    return history[i - 2].title
  end)
end

function draw()
  if article then layout_article() end
  if article and article.sel then
    hints_height = gfx.hints("Cancel", "Open", "<", ">")
  elseif article then
    hints_height = gfx.hints("Back", article.links and #article.links > 0 and "Links" or "Search", "Prev", "Next")
  elseif results then
    hints_height = gfx.hints("Back", "Open", nil, nil)
  else
    hints_height = gfx.hints("Exit", pack and "Select" or nil, nil, nil)
  end
  local bottom = gfx.height() - hints_height - 8
  if article then draw_article()
  elseif results then draw_results(bottom)
  else draw_home(bottom) end
end

-- Input ------------------------------------------------------------------------

local function activate_home(row)
  if not pack then return end
  if row == 1 then prompt()
  elseif row == 2 then open_article(pack:random())
  else open_article(history[row - 2].id) end
end

local function go_back()
  if article and article.sel then
    article.sel = nil
  elseif article then
    if not retrace() then close_article() end
  elseif results then
    results = nil
  elseif message then
    message = nil
  else
    return false
  end
  return true
end

function on_button(button)
  if button == "back" then
    if not go_back() then return false end
    sys.redraw()
    return true
  end
  if article and article.sel then
    local n = #article.links
    if button == "down" or button == "right" then article.sel = article.sel % n + 1
    elseif button == "up" or button == "left" then article.sel = (article.sel - 2) % n + 1
    elseif button == "confirm" then follow(article.links[article.sel]) end
  elseif article then
    if button == "down" or button == "right" then turn(1)
    elseif button == "up" or button == "left" then turn(-1)
    elseif button == "confirm" then
      if article.links and #article.links > 0 then article.sel = 1 else prompt(results and results.query) return end
    end
  elseif results then
    if button == "up" then results.view:move(-1)
    elseif button == "down" then results.view:move(1)
    elseif button == "confirm" then open_article(results.hits[results.view.selected].id) end
  else
    message = nil
    if button == "up" then home_view:move(-1)
    elseif button == "down" then home_view:move(1)
    elseif button == "confirm" then activate_home(home_view.selected) end
  end
  sys.redraw()
end

function on_swipe(direction)
  if article then
    if direction == "left" or direction == "up" then turn(1) else turn(-1) end
  elseif results then
    if direction == "up" then results.view:page(1) elseif direction == "down" then results.view:page(-1) end
  else
    if direction == "up" then home_view:page(1) elseif direction == "down" then home_view:page(-1) end
  end
  sys.redraw()
end

function on_touch(x, y, kind)
  if article then
    local target = kind ~= "long" and link_at(x, y)
    if y < header_bottom then go_back()
    elseif kind == "long" then prompt(results and results.query)
    elseif target then follow(target)
    else turn(x < gfx.width() // 2 and -1 or 1) end
  elseif results then
    if y < header_bottom then go_back() else
      local hit, row = results.view:tap(x, y)
      if hit == "row" then open_article(results.hits[row].id) end
    end
  else
    message = nil
    local hit, row = home_view:tap(x, y)
    if hit == "row" then activate_home(row) end
  end
  sys.redraw()
end

load_history()
