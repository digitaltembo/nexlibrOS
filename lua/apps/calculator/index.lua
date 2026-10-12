-- title: Calculator
--
-- Keys: Up/Down/Left/Right move the highlight over the keypad, Confirm
-- presses the highlighted key, Back exits. Touch: tap a key.
--
-- Entries build an expression that = evaluates with the usual precedence
-- (× and ÷ before + and -); a live result shows as you type. % divides the
-- current number by 100, ± flips its sign. Numbers are the device's 32-bit
-- floats, so results show 7 significant digits.

local MARGIN = 12
local GAP = 8
local MAX_DIGITS = 12
local KEYS = {
  { "C", "DEL", "%", "÷" },
  { "7", "8", "9", "×" },
  { "4", "5", "6", "-" },
  { "1", "2", "3", "+" },
  { "±", "0", ".", "=" },
}
local PRECEDENCE = { ["+"] = 1, ["-"] = 1, ["×"] = 2, ["÷"] = 2 }

local tokens = {} -- alternating number text and operators, starting with a number
local last = nil -- the evaluated expression, shown above its result
local evaluated = false -- tokens[1] is a result: a digit starts a new expression
local error_text = nil
local row, col = 5, 2 -- highlighted key ("0")
local keypad = nil -- { x, y, w, h } of the first key, for touch

-- Numbers ------------------------------------------------------------------------

-- Seven significant digits: what a 32-bit float holds.
local function format(v)
  if v ~= v or v == math.huge or v == -math.huge then return nil end
  if v == 0 then return "0" end -- also -0
  local s = string.format("%.7g", v)
  return (s:gsub("e%+?(%-?)0*(%d)", "e%1%2")) -- "1.5e+07" -> "1.5e7"
end

local function number_of(text)
  return (tonumber(text) or 0) + 0.0 -- float: 32-bit integers would wrap
end

-- Evaluates tokens (ignoring a trailing operator); nil, err on failure.
local function evaluate()
  local nums, ops = {}, {}
  local function apply()
    local b, a, op = table.remove(nums), table.remove(nums), table.remove(ops)
    if op == "+" then
      a = a + b
    elseif op == "-" then
      a = a - b
    elseif op == "×" then
      a = a * b
    else
      if b == 0 then return false end
      a = a / b
    end
    nums[#nums + 1] = a
    return true
  end
  local count = #tokens
  if PRECEDENCE[tokens[count]] then count = count - 1 end
  if count == 0 then return nil, "empty" end
  for i = 1, count do
    local t = tokens[i]
    if PRECEDENCE[t] then
      while #ops > 0 and PRECEDENCE[ops[#ops]] >= PRECEDENCE[t] do
        if not apply() then return nil, "Can't divide by zero" end
      end
      ops[#ops + 1] = t
    else
      nums[#nums + 1] = number_of(t)
    end
  end
  while #ops > 0 do
    if not apply() then return nil, "Can't divide by zero" end
  end
  local text = format(nums[1])
  if not text then return nil, "Result out of range" end
  return text
end

-- Keys ---------------------------------------------------------------------------

-- Index of the trailing number token, or nil when the expression is empty or
-- ends with an operator.
local function current()
  local t = tokens[#tokens]
  if t and not PRECEDENCE[t] then return #tokens end
  return nil
end

local function digit_count(text)
  return #text:gsub("[^%d]", "")
end

local function clear()
  tokens, last, evaluated = {}, nil, false
end

local function press(key)
  error_text = nil
  local i = current()
  if key == "C" or (key == "DEL" and evaluated) then
    clear()
  elseif key:match("^%d$") then
    if evaluated then
      clear()
      i = nil
    end
    if not i then
      tokens[#tokens + 1] = key
    elseif digit_count(tokens[i]) < MAX_DIGITS and not tokens[i]:find("e", 1, true) then
      local t = tokens[i]
      tokens[i] = (t == "0" and key) or (t == "-0" and "-" .. key) or t .. key
    end
  elseif key == "." then
    if evaluated then
      clear()
      i = nil
    end
    if not i then
      tokens[#tokens + 1] = "0."
    elseif not tokens[i]:find("[.e]") then
      tokens[i] = tokens[i] .. "."
    end
  elseif PRECEDENCE[key] then
    if #tokens == 0 then tokens[1] = "0" end
    if i then tokens[#tokens + 1] = key else tokens[#tokens] = key end
    evaluated = false
  elseif key == "=" then
    if evaluated or #tokens == 0 then return end
    local result, err = evaluate()
    if not result then
      error_text = err
      return
    end
    if PRECEDENCE[tokens[#tokens]] then tokens[#tokens] = nil end
    last = table.concat(tokens, " ") .. " ="
    tokens, evaluated = { result }, true
  elseif key == "DEL" then
    local t = tokens[#tokens]
    if t then
      t = PRECEDENCE[t] and "" or t:sub(1, -2)
      tokens[#tokens] = (t ~= "" and t ~= "-") and t or nil
    end
  elseif key == "±" then
    if evaluated then evaluated = false end
    if not i then
      tokens[#tokens + 1] = "-0" -- a bare "-" would read as the minus operator
    else
      local t = tokens[i]
      tokens[i] = t:sub(1, 1) == "-" and t:sub(2) or "-" .. t
      if tokens[i] == "" then tokens[i] = nil end
    end
  elseif key == "%" and i then
    tokens[i] = format(number_of(tokens[i]) / 100) or tokens[i]
  end
end

-- Drawing ------------------------------------------------------------------------

-- Right-aligned lines of text ending at y_bottom; returns the top line's y.
local function draw_right(text, right, y_bottom, width, max_lines)
  local lines = gfx.wrap(text, width, 32)
  local lh = gfx.line_height()
  local first = math.max(1, #lines - max_lines + 1) -- keep the end of a long expression
  local y = y_bottom - (#lines - first + 1) * lh
  local top = y
  for k = first, #lines do
    gfx.text(right - gfx.text_width(lines[k]), y, lines[k])
    y = y + lh
  end
  return top
end

local function draw_display(top, bottom)
  local w = gfx.width()
  local right, width = w - MARGIN - 4, w - 2 * MARGIN - 8
  local expression = #tokens > 0 and table.concat(tokens, " ") or "0"
  local y = bottom
  if error_text then
    gfx.font("ui", "bold")
    y = draw_right(error_text, right, y, width, 2) - 6
  elseif not evaluated and #tokens > 1 then
    local preview = evaluate()
    if preview then
      gfx.font("sans14")
      y = draw_right("= " .. preview, right, y, width, 1) - 6
    end
  end
  gfx.font("sans18", "bold")
  y = draw_right(expression, right, y, width, 3) - 6
  if evaluated and last then
    gfx.font("sans14")
    draw_right(last, right, y, width, math.max(1, (y - top) // gfx.line_height()))
  end
end

local function draw_key(label, x, y, kw, kh, selected)
  if selected then
    gfx.fill_round_rect(x, y, kw, kh, 10)
  else
    gfx.round_rect(x, y, kw, kh, 10)
    if PRECEDENCE[label] or label == "=" then gfx.round_rect(x + 1, y + 1, kw - 2, kh - 2, 9) end
  end
  gfx.font(label:match("^%d$") and "sans18" or "ui_large", "bold")
  gfx.text(x + (kw - gfx.text_width(label)) // 2, y + (kh - gfx.line_height()) // 2, label, not selected)
end

function draw()
  local w, h = gfx.width(), gfx.height()
  local bottom = h - gfx.hints("Exit", "Press", "Left", "Right") - GAP
  local cols, rows = #KEYS[1], #KEYS
  local kw = (w - 2 * MARGIN - (cols - 1) * GAP) // cols
  -- Keys take the lower part of the screen, the display the rest.
  local kh = math.min(kw, ((bottom - MARGIN) * 2 // 3 - (rows - 1) * GAP) // rows)
  local pad_top = bottom - rows * kh - (rows - 1) * GAP
  keypad = { x = MARGIN, y = pad_top, w = kw, h = kh }
  draw_display(MARGIN, pad_top - GAP * 2)
  gfx.line(MARGIN, pad_top - GAP, w - MARGIN, pad_top - GAP)
  for r, keys in ipairs(KEYS) do
    for c, label in ipairs(keys) do
      draw_key(label, MARGIN + (c - 1) * (kw + GAP), pad_top + (r - 1) * (kh + GAP), kw, kh, r == row and c == col)
    end
  end
end

-- Input ------------------------------------------------------------------------------

function on_button(button)
  if button == "back" then return false end
  if button == "up" then
    row = (row - 2) % #KEYS + 1
  elseif button == "down" then
    row = row % #KEYS + 1
  elseif button == "left" then
    col = (col - 2) % #KEYS[1] + 1
  elseif button == "right" then
    col = col % #KEYS[1] + 1
  elseif button == "confirm" then
    press(KEYS[row][col])
  end
  sys.redraw()
  return true
end

function on_touch(x, y)
  local k = keypad
  if not k then return end
  local c = (x - k.x) // (k.w + GAP) + 1
  local r = (y - k.y) // (k.h + GAP) + 1
  -- Ignore taps in the gaps between keys.
  local inside = (x - k.x) % (k.w + GAP) < k.w and (y - k.y) % (k.h + GAP) < k.h
  if inside and KEYS[r] and KEYS[r][c] and x >= k.x and y >= k.y then
    row, col = r, c
    press(KEYS[r][c])
    sys.redraw()
  end
end
