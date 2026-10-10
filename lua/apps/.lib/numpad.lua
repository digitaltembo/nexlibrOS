-- On-screen number pad, a numeric stand-in for sys.keyboard. open() takes
-- over the app's draw/on_button/on_touch globals and restores them before
-- calling back, so the pad's code is released once it closes when loaded
-- with load() rather than require().
--
--   local numpad = require("numpad")
--   numpad.open({
--     title = "Amount",
--     text = "12.5",                  -- initial text; the first key typed replaces it
--     max_len = 24,
--     keys = numpad.DECIMAL,          -- optional rows, or a preset name ("INTEGER"); see below
--     note = "± for BC",              -- optional line under the field
--     check = function(text) ... end, -- optional: an error message keeps the pad open
--     done = function(text) ... end,  -- text on OK, nil on Back
--   })
--
-- Keys are rows of labels. A label typed into the text as-is, except:
--   DEL    deletes the last character      C   clears the text
--   SPACE  types a space                   OK  finishes
--   ±      toggles the sign of the number (the exponent's, right after an "E")
-- A label repeated in adjacent cells of a row draws as one wide key.
--
-- Buttons: Up/Down/Left/Right move the highlight, Confirm presses the key,
-- Back cancels. Touch: tap a key.

local numpad = {}

numpad.DECIMAL = {
  { "7", "8", "9", "DEL" },
  { "4", "5", "6", "C" },
  { "1", "2", "3", "±" },
  { "0", "0", ".", "OK" },
}

numpad.INTEGER = {
  { "7", "8", "9", "DEL" },
  { "4", "5", "6", "C" },
  { "1", "2", "3", "±" },
  { "0", "0", "OK", "OK" },
}

local MARGIN = 12
local GAP = 8
local LABELS = { SPACE = "Space" }

local function find_key(keys, label)
  for r, row in ipairs(keys) do
    for c, l in ipairs(row) do
      if l == label then return r, c end
    end
  end
  return 1, 1
end

-- First and last column of the key covering keys[r][c].
local function span(keys, r, c)
  local row = keys[r]
  local first, last = c, c
  while first > 1 and row[first - 1] == row[c] do first = first - 1 end
  while last < #row and row[last + 1] == row[c] do last = last + 1 end
  return first, last
end

local function toggle_sign(text)
  local head, exp = text:match("^(.*[eE])(%-?%d*)$")
  if head then
    return head .. (exp:sub(1, 1) == "-" and exp:sub(2) or "-" .. exp)
  end
  return text:sub(1, 1) == "-" and text:sub(2) or "-" .. text
end

-- Last characters of text that fit width, with a leading "..." when cut.
local function tail(text, width)
  if gfx.text_width(text) <= width then return text end
  local i = 1
  while i < #text and gfx.text_width("..." .. text:sub(i)) > width do i = i + 1 end
  return "..." .. text:sub(i)
end

function numpad.open(opts)
  local keys = opts.keys or numpad.DECIMAL
  if type(keys) == "string" then keys = assert(numpad[keys], "unknown numpad keys") end
  local max_len = opts.max_len or 32
  local text = opts.text or ""
  local fresh = text ~= "" -- the initial text is replaced by the first key typed
  local error_text = nil
  local row, col = find_key(keys, text ~= "" and "OK" or "1")
  local pad = nil -- { x, y, w, h } of the first key, for touch
  local closed = false
  local saved_draw, saved_button, saved_touch = draw, on_button, on_touch

  local function close(result)
    closed = true
    draw, on_button, on_touch = saved_draw, saved_button, saved_touch
    opts.done(result)
    sys.redraw()
  end

  local function press(label)
    error_text = nil
    if label == "OK" then
      local err = opts.check and opts.check(text)
      if err then
        error_text = err
      else
        close(text)
      end
      return
    end
    if label == "C" then
      text = ""
    elseif label == "DEL" then
      text = fresh and "" or text:sub(1, -2)
    elseif label == "±" then
      local t = toggle_sign(text)
      if #t <= max_len then text = t end
    else
      local s = label == "SPACE" and " " or label
      if fresh then text = "" end
      if #text + #s <= max_len then text = text .. s end
    end
    fresh = false
  end

  local function move_col(delta)
    local first, last = span(keys, row, col)
    col = ((delta > 0 and last or first) - 1 + delta) % #keys[row] + 1
  end

  local function draw_key(label, x, y, kw, kh, selected)
    if selected then
      gfx.fill_round_rect(x, y, kw, kh, 10)
    else
      gfx.round_rect(x, y, kw, kh, 10)
      if label == "OK" then gfx.round_rect(x + 1, y + 1, kw - 2, kh - 2, 9) end
    end
    local shown = LABELS[label] or label
    gfx.font(label:match("^%d$") and "sans18" or "ui_large", "bold")
    gfx.text(x + (kw - gfx.text_width(shown)) // 2, y + (kh - gfx.line_height()) // 2, shown, not selected)
  end

  local function draw_pad()
    local w, h = gfx.width(), gfx.height()
    local bottom = h - gfx.hints("Cancel", "Press", "Left", "Right") - GAP
    local cols, rows = #keys[1], #keys
    local kw = (w - 2 * MARGIN - (cols - 1) * GAP) // cols
    local kh = math.min(kw, ((bottom - MARGIN) * 2 // 3 - (rows - 1) * GAP) // rows)
    local pad_top = bottom - rows * kh - (rows - 1) * GAP
    pad = { x = MARGIN, y = pad_top, w = kw, h = kh }

    gfx.font("ui_large", "bold")
    gfx.text(MARGIN, MARGIN, gfx.wrap(opts.title or "", w - 2 * MARGIN, 1)[1] or "")
    local y = MARGIN + gfx.line_height() + GAP
    gfx.font("sans18", "bold")
    local field_h = gfx.line_height() + 16
    local inner = w - 2 * MARGIN - 24
    gfx.round_rect(MARGIN, y, w - 2 * MARGIN, field_h, 6)
    local shown = tail(text, inner - 4)
    local tx = MARGIN + 12
    if fresh then
      gfx.fill_rect(tx - 4, y + 6, gfx.text_width(shown) + 8, field_h - 12)
      gfx.text(tx, y + 8, shown, false)
    else
      gfx.text(tx, y + 8, shown)
      local cx = tx + gfx.text_width(shown) + 2
      gfx.line(cx, y + 8, cx, y + field_h - 8, true, 2)
    end
    y = y + field_h + GAP
    local below = error_text or opts.note
    if below then
      gfx.font("ui", error_text and "bold" or "regular")
      for _, l in ipairs(gfx.wrap(below, w - 2 * MARGIN, math.max(1, (pad_top - GAP - y) // gfx.line_height()))) do
        gfx.text(MARGIN, y, l)
        y = y + gfx.line_height()
      end
    end

    for r, labels in ipairs(keys) do
      local c = 1
      while c <= #labels do
        local first, last = span(keys, r, c)
        local x = MARGIN + (first - 1) * (kw + GAP)
        local kx = (last - first + 1) * kw + (last - first) * GAP
        draw_key(labels[c], x, pad_top + (r - 1) * (kh + GAP), kx, kh, r == row and col >= first and col <= last)
        c = last + 1
      end
    end
  end

  draw = draw_pad

  on_button = function(button)
    if button == "back" then
      close(nil)
      return true
    end
    if button == "up" then
      row = (row - 2) % #keys + 1
    elseif button == "down" then
      row = row % #keys + 1
    elseif button == "left" then
      move_col(-1)
    elseif button == "right" then
      move_col(1)
    elseif button == "confirm" then
      press(keys[row][col])
      if closed then return true end
    end
    col = math.min(col, #keys[row])
    sys.redraw()
    return true
  end

  on_touch = function(x, y)
    local k = pad
    if not k or x < k.x or y < k.y then return end
    local c = (x - k.x) // (k.w + GAP) + 1
    local r = (y - k.y) // (k.h + GAP) + 1
    local label = keys[r] and keys[r][c]
    if not label then return end
    -- Gaps between keys are dead, except inside a wide key.
    local _, last = span(keys, r, c)
    local in_x = (x - k.x) % (k.w + GAP) < k.w or c < last
    if not (in_x and (y - k.y) % (k.h + GAP) < k.h) then return end
    row, col = r, c
    press(label)
    if not closed then sys.redraw() end
  end

  sys.redraw()
end

return numpad
