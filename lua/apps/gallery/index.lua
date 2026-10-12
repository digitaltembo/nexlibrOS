-- title: Gallery
-- description: Photo browser and low-power slideshow
--
-- Put folders of JPEG, PNG or BMP images in /apps/gallery (folders may nest).
--
-- Browser: Left/Right move, Up/Down move a row, Confirm opens a folder or
-- photo, Back goes up a folder. The Slideshow tile plays the folder.
-- Photo: Left/Right (or Up/Down) step, Confirm plays or pauses the slideshow,
-- Back returns to the folder.
--
-- Photos are decoded once into grey .pxc caches under .data/cache, one per
-- photo for thumbnails and one for full screen. While a slideshow plays the
-- device stays awake but idles at a low clock (and, with Low-power idle on, in
-- light sleep) between slides; pausing lets it sleep as usual.

local listview = require("listview")
local store = require("store")

local W, H = gfx.width(), gfx.height()
local MARGIN = 12
local GAP = 10
local PAD = 6
local HEADER_H = 44
local COLS = W > H and 4 or 3
local ROWS = W > H and 2 or 3
local PER_PAGE = COLS * ROWS
local MAX_SLIDES = 300
local JOB_TICK_MS = 250
local SLIDE_TICK_MS = 1000

local INTERVALS = { 10, 30, 60, 120, 300, 600, 900, 1800, 3600 }

local settings = store.settings
store.load_settings()

-- Background work: one step per tick so input stays responsive and a frame
-- showing progress is drawn in between.
local job = nil

local mode = "browser" -- "browser", "viewer" or "settings"

-- Browser state.
local cwd = store.ROOT
-- Tiles: the specials ("play", "settings"), then folders, then photos. Plain
-- name arrays keep a folder of hundreds of photos small on the Lua heap.
local specials = {}
local dirs, images = {}, {}
local selected = 1
local shown_page = nil
local grid = nil -- tile geometry, fixed once the hint bar height is known
local thumbs = {} -- visible page's paths -> { file, w, h }, or false when unreadable
local pending = {} -- thumbnail paths still to cache on this page
local pending_total = 0

-- Viewer state.
local list = {} -- photos being viewed, as paths relative to list_dir
local list_dir = nil
local index = 1
local photo = nil -- { file, w, h } for the current photo
local photo_error = nil
local playing = false
local next_slide_at = 0
local order = nil -- shuffled indices into list

local settings_view = listview.new(56)
local notice = nil

local function update_tick()
  if job then
    sys.set_tick(JOB_TICK_MS)
  elseif playing then
    sys.set_tick(SLIDE_TICK_MS)
  else
    sys.set_tick(0)
  end
end

local function run_job(fn)
  job = fn
  update_tick()
end

local function interval_label(seconds)
  if seconds < 60 then return seconds .. " s" end
  if seconds < 3600 then return (seconds // 60) .. " min" end
  return (seconds // 3600) .. " h"
end

local function relative(path)
  if path == store.ROOT then return "Gallery" end
  return path:sub(#store.ROOT + 2)
end

local function basename(path)
  return path:match("([^/]+)$") or path
end

-- Browser ----------------------------------------------------------------------

local function entry_count()
  return #specials + #dirs + #images
end

-- Returns the kind ("play", "settings", "dir" or "image"), label and path of
-- tile i.
local function entry(i)
  if i <= #specials then
    local kind = specials[i]
    return kind, kind == "play" and "Slideshow" or "Settings"
  end
  i = i - #specials
  if i <= #dirs then return "dir", dirs[i], cwd .. "/" .. dirs[i] end
  local name = images[i - #dirs]
  return "image", name, cwd .. "/" .. name
end

local function page_bounds()
  local first = (selected - 1) // PER_PAGE * PER_PAGE + 1
  return first, math.min(entry_count(), first + PER_PAGE - 1)
end

local function thumb_box()
  return grid.tile_w - 2 * PAD, grid.tile_h - grid.label_h - 2 * PAD
end

local function cache_next_thumb()
  local path = table.remove(pending, 1)
  if not path then return end
  local bw, bh = thumb_box()
  local file, w, h = store.prepare(path, bw, bh)
  thumbs[path] = file and { file, w, h } or false
  if #pending > 0 then run_job(cache_next_thumb) end
  sys.redraw()
end

-- Finds the cached thumbnails of the visible page and queues the rest.
local function load_page()
  if not grid then return end
  shown_page = (selected - 1) // PER_PAGE
  pending = {}
  local known = thumbs
  thumbs = {}
  local bw, bh = thumb_box()
  local first, last = page_bounds()
  for i = first, last do
    local kind, _, path = entry(i)
    if kind == "image" then
      if known[path] ~= nil then
        thumbs[path] = known[path]
      else
        local file, w, h = store.lookup(path, bw, bh)
        if file then
          thumbs[path] = { file, w, h }
        elseif w then
          thumbs[path] = false -- unreadable BMP
        else
          pending[#pending + 1] = path
        end
      end
    end
  end
  pending_total = #pending
  if #pending > 0 then run_job(cache_next_thumb) end
end

local function open_dir(path, select_path)
  cwd = path
  thumbs = {}
  dirs, images = {}, {} -- release the old lists before scanning
  dirs, images = store.scan(path)
  specials = {}
  if #images > 0 or (settings.subfolders and #dirs > 0) then specials[#specials + 1] = "play" end
  if path == store.ROOT then specials[#specials + 1] = "settings" end
  selected = 1
  for i = 1, entry_count() do
    local _, _, p = entry(i)
    if p == select_path then
      selected = i
      break
    end
  end
  load_page()
end

local function move_selection(delta)
  local count = entry_count()
  if count == 0 then return end
  selected = (selected - 1 + delta) % count + 1
  if (selected - 1) // PER_PAGE ~= shown_page then load_page() end
  sys.redraw()
end

local function draw_folder_icon(cx, cy, size, black)
  local w, h = size, size * 3 // 4
  local x, y = cx - w // 2, cy - h // 2
  gfx.fill_round_rect(x, y, w // 2, h // 4, 3, black)
  gfx.fill_round_rect(x, y + h // 6, w, h - h // 6, 4, black)
end

local function draw_tile(i, x, y, w, h)
  local kind, name, path = entry(i)
  local bw, bh = thumb_box()
  local bx, by = x + PAD, y + PAD
  if kind == "image" then
    local thumb = thumbs[path]
    if thumb and not job then
      local file, tw, th = thumb[1], thumb[2], thumb[3]
      if not gfx.photo(file, bx + (bw - tw) // 2, by + (bh - th) // 2, tw, th) then thumbs[path] = false end
    elseif thumb == false then
      gfx.rect(bx, by, bw, bh)
      gfx.line(bx, by, bx + bw - 1, by + bh - 1)
      gfx.line(bx + bw - 1, by, bx, by + bh - 1)
    else
      gfx.fill_rect(bx, by, bw, bh, "light_gray")
    end
  else
    local cx, cy = bx + bw // 2, by + bh // 2
    local size = math.min(bw, bh) // 2
    if kind == "dir" then
      draw_folder_icon(cx, cy, size)
    elseif kind == "play" then
      gfx.fill_polygon({ cx - size // 3, cy - size // 2, cx - size // 3, cy + size // 2, cx + size // 2, cy })
    else
      gfx.circle(cx, cy, size // 2)
      gfx.fill_circle(cx, cy, size // 5)
    end
  end
  gfx.font("small")
  local label = gfx.wrap(name, w - 2 * PAD, 1)[1] or ""
  gfx.text(x + (w - gfx.text_width(label)) // 2, y + h - grid.label_h, label)
  if i == selected then
    for i = 0, 2 do gfx.rect(x + i, y + i, w - 2 * i, h - 2 * i) end
  end
end

local function draw_browser()
  local hint_h = gfx.hints(cwd == store.ROOT and "Exit" or "Back", "Open", "<", ">")
  if not grid then
    gfx.font("small")
    local tile_w = (W - 2 * MARGIN - (COLS - 1) * GAP) // COLS
    local tile_h = (H - HEADER_H - hint_h - MARGIN - (ROWS - 1) * GAP) // ROWS
    grid = { tile_w = tile_w, tile_h = tile_h, label_h = gfx.line_height() + 6 }
    load_page()
  end

  gfx.font("ui_large", "bold")
  local title = gfx.wrap(relative(cwd), W - 2 * MARGIN - 120, 1)[1] or ""
  gfx.text(MARGIN, 10, title)
  gfx.font("ui")
  local status
  if job and pending_total > 0 then
    status = string.format("Preparing %d/%d", pending_total - #pending, pending_total)
  elseif entry_count() > PER_PAGE then
    status = string.format("%d/%d", (selected - 1) // PER_PAGE + 1, (entry_count() - 1) // PER_PAGE + 1)
  end
  if status then gfx.text(W - MARGIN - gfx.text_width(status), 14, status) end
  gfx.line(MARGIN, HEADER_H - 4, W - MARGIN, HEADER_H - 4)

  if #dirs + #images == 0 then
    gfx.font("ui")
    local lines = gfx.wrap("No photos here. Copy folders of JPEG, PNG or BMP images to " .. store.ROOT
      .. " on the SD card.", W - 4 * MARGIN)
    local top = HEADER_H + (#specials > 0 and grid.tile_h + GAP or 0) + 20
    for i, line in ipairs(lines) do gfx.text(2 * MARGIN, top + (i - 1) * gfx.line_height(), line) end
  end

  local first, last = page_bounds()
  for i = first, last do
    local slot = i - first
    local col, row = slot % COLS, slot // COLS
    local x = MARGIN + col * (grid.tile_w + GAP)
    local y = HEADER_H + row * (grid.tile_h + GAP)
    draw_tile(i, x, y, grid.tile_w, grid.tile_h)
  end
end

-- Viewer and slideshow -------------------------------------------------------

local function current_path()
  return list_dir .. "/" .. list[index]
end

local function show(i)
  index = (i - 1) % #list + 1
  photo, photo_error = nil, nil
  local file, w, h = store.lookup(current_path(), W, H)
  if file then
    photo = { file, w, h }
    sys.redraw("half")
    return
  end
  if w then
    photo_error = w
    sys.redraw()
    return
  end
  sys.redraw()
  run_job(function()
    local f, fw, fh = store.prepare(current_path(), W, H)
    if f then photo = { f, fw, fh } else photo_error = fw end
    sys.redraw("half")
  end)
end

local function shuffle_order()
  order = {}
  for i = 1, #list do order[i] = i end
  for i = #list, 2, -1 do
    local j = math.random(i)
    order[i], order[j] = order[j], order[i]
  end
end

local function set_playing(on)
  playing = on and #list > 0
  next_slide_at = sys.millis() + settings.interval * 1000
  sys.keep_awake(playing)
  sys.light_sleep(playing and settings.low_power)
  update_tick()
end

-- Steps the slideshow, decoding in place so the old photo stays up until the
-- new one is ready; unreadable photos are skipped.
local function step(delta)
  for _ = 1, math.min(#list, 8) do
    local pos = index
    if order then
      for k, v in ipairs(order) do
        if v == index then pos = k break end
      end
      index = order[(pos - 1 + delta) % #order + 1]
    else
      index = (index - 1 + delta) % #list + 1
    end
    local file, w, h = store.prepare(current_path(), W, H)
    if file then
      photo, photo_error = { file, w, h }, nil
      sys.redraw("half")
      return
    end
  end
  photo, photo_error = nil, "No readable photos"
  set_playing(false)
  sys.redraw()
end

local function open_viewer(dir, paths, start, play)
  list, list_dir = paths, dir
  order = nil
  if play and settings.shuffle then
    shuffle_order()
    start = order[1]
  end
  mode = "viewer"
  show(start)
  set_playing(play)
end

local function close_viewer()
  set_playing(false)
  job = nil
  update_tick()
  mode = "browser"
  local current = current_path()
  list, order, photo = {}, nil, nil
  for i = 1, entry_count() do
    local _, _, path = entry(i)
    if path == current then
      selected = i
      break
    end
  end
  load_page()
  sys.redraw("half")
end

local function draw_viewer()
  if photo then
    local file, w, h = photo[1], photo[2], photo[3]
    local ok, err = gfx.photo(file, (W - w) // 2, (H - h) // 2, w, h)
    if ok then return end
    photo, photo_error = nil, err
  end
  gfx.font("ui")
  local text = photo_error and (basename(current_path()) .. ": " .. photo_error) or ("Loading " .. basename(current_path()))
  local lines = gfx.wrap(text, W - 4 * MARGIN, 4)
  local y = H // 2 - #lines * gfx.line_height() // 2
  for _, line in ipairs(lines) do
    gfx.text((W - gfx.text_width(line)) // 2, y, line)
    y = y + gfx.line_height()
  end
end

-- Settings -------------------------------------------------------------------

local function cycle_interval(delta)
  local at = 1
  for i, v in ipairs(INTERVALS) do
    if v == settings.interval then at = i end
  end
  settings.interval = INTERVALS[(at - 1 + delta) % #INTERVALS + 1]
end

local SETTING_ROWS = {
  { label = "Slide every", value = function() return interval_label(settings.interval) end,
    change = cycle_interval },
  { label = "Order", value = function() return settings.shuffle and "Shuffle" or "In order" end,
    change = function() settings.shuffle = not settings.shuffle end },
  { label = "Include subfolders", value = function() return settings.subfolders and "On" or "Off" end,
    change = function() settings.subfolders = not settings.subfolders end },
  { label = "Low-power idle", value = function() return settings.low_power and "On" or "Off" end,
    change = function() settings.low_power = not settings.low_power end },
  { label = "Clear photo cache", value = function() return "" end,
    change = function()
      notice = string.format("Removed %d cached photos", store.clear_cache())
      thumbs = {}
    end },
}

local function draw_settings()
  local hint_h = gfx.hints("Back", "Change", "-", "+")
  gfx.font("ui_large", "bold")
  gfx.text(MARGIN, 10, "Gallery settings")
  gfx.line(MARGIN, HEADER_H - 4, W - MARGIN, HEADER_H - 4)
  local bottom = H - hint_h - (notice and 40 or 8)
  settings_view:set_bounds(MARGIN, HEADER_H + 4, W - 2 * MARGIN, bottom - HEADER_H - 4)
  settings_view:draw(#SETTING_ROWS, function(i, x, y, w, h, fg)
    local row = SETTING_ROWS[i]
    gfx.font("ui")
    local ty = y + (h - gfx.line_height()) // 2
    gfx.text(x + 12, ty, row.label, fg)
    local value = row.value()
    gfx.text(x + w - 12 - gfx.text_width(value), ty, value, fg)
  end)
  if notice then
    gfx.font("ui")
    gfx.text(MARGIN, H - hint_h - 32, notice)
  end
end

local function close_settings()
  store.save_settings()
  mode = "browser"
  notice = nil
  open_dir(cwd)
  sys.redraw()
end

-- Input ----------------------------------------------------------------------

local function activate(i)
  local kind, _, path = entry(i)
  if kind == "dir" then
    open_dir(path)
    sys.redraw()
  elseif kind == "settings" then
    mode = "settings"
    job = nil
    update_tick()
    sys.redraw()
  elseif kind == "play" then
    local paths = settings.subfolders and store.collect(cwd, MAX_SLIDES) or images
    if #paths > 0 then open_viewer(cwd, paths, 1, true) end
  elseif kind == "image" then
    open_viewer(cwd, images, i - #specials - #dirs, false)
  end
end

function on_tick()
  if job then
    local fn = job
    job = nil
    fn()
    update_tick()
    return
  end
  if playing and sys.millis() - next_slide_at >= 0 then
    next_slide_at = sys.millis() + settings.interval * 1000
    step(1)
  end
end

function on_button(button)
  notice = mode == "settings" and notice or nil
  if mode == "viewer" then
    if button == "back" then
      close_viewer()
    elseif button == "confirm" then
      if not playing and settings.shuffle and not order then shuffle_order() end
      set_playing(not playing)
    elseif button == "left" or button == "up" then
      if playing then
        next_slide_at = sys.millis() + settings.interval * 1000
        step(-1)
      else
        show(index - 1)
      end
    elseif button == "right" or button == "down" then
      if playing then
        next_slide_at = sys.millis() + settings.interval * 1000
        step(1)
      else
        show(index + 1)
      end
    end
    return true
  end

  if mode == "settings" then
    local row = SETTING_ROWS[settings_view.selected]
    if button == "back" then
      close_settings()
    elseif button == "up" then
      settings_view:move(-1)
    elseif button == "down" then
      settings_view:move(1)
    elseif button == "confirm" or button == "right" then
      row.change(1)
    elseif button == "left" then
      row.change(-1)
    end
    sys.redraw()
    return true
  end

  if button == "back" then
    if cwd == store.ROOT then return false end
    local from = cwd
    open_dir(cwd:match("^(.*)/[^/]+$") or store.ROOT, from)
    sys.redraw()
  elseif button == "confirm" then
    activate(selected)
  elseif button == "left" then
    move_selection(-1)
  elseif button == "right" then
    move_selection(1)
  elseif button == "up" then
    move_selection(-COLS)
  elseif button == "down" then
    move_selection(COLS)
  end
  return true
end

function on_touch(x, y, kind)
  if mode == "viewer" then
    if kind == "long" then return on_button("confirm") end
    return on_button(x < W // 2 and "left" or "right")
  end
  if mode == "settings" then
    if settings_view:tap(x, y) == "row" then SETTING_ROWS[settings_view.selected].change(1) end
    sys.redraw()
    return
  end
  if not grid or y < HEADER_H then return end
  local col = (x - MARGIN) // (grid.tile_w + GAP)
  local row = (y - HEADER_H) // (grid.tile_h + GAP)
  if col < 0 or col >= COLS or row < 0 or row >= ROWS then return end
  local first = page_bounds()
  local i = first + row * COLS + col
  if i <= entry_count() then
    selected = i
    activate(i)
  end
end

function on_swipe(direction)
  if mode == "viewer" then
    on_button(direction == "left" and "right" or "left")
  elseif mode == "browser" and entry_count() > PER_PAGE then
    move_selection(direction == "left" and PER_PAGE or -PER_PAGE)
  end
end

function draw()
  if mode == "viewer" then
    draw_viewer()
  elseif mode == "settings" then
    draw_settings()
  else
    draw_browser()
  end
end

open_dir(store.ROOT)
