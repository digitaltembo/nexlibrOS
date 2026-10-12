-- The Converter app's resident core, required by index.lua once catalog.txt
-- is current: state, the form, drawing and input. Defines the app's
-- draw/on_button/on_touch globals and returns start(stamp).
--
-- Code needed only for a moment is loaded with C.use() rather than require(),
-- which would keep it for the app's lifetime, and is released when done:
--   contents.lua reads catalog.txt's section table at start
--   pickers.lua  while a picker is open
--   coins.lua    a currency's coins and number formatting, for pickers.lua
--                and money.lua
--   money.lua    each conversion, with jsonstream to read the JSON records
--   numpad.lua   (/apps/.lib) while the number pad is open
--
-- Numbers are kept as { mantissa, exponent } pairs: the device's 32-bit floats
-- cannot hold values such as a litre in Planck volumes.

local listview = require("listview")

local MARGIN = 16

local S = {
  catalog = nil, -- see contents.lua
  message = nil,
  amount = nil, -- { text, value }
  from = nil, -- selections, see pickers.lua
  to = nil,
  result = nil, -- { main, label, lines = { ... } }
  picker = nil, -- { target = "from" | "to", stack = { { title, items, view } } }
  after_pick = false, -- a picker finished; convert once its code is released
}

-- Shared with the modules C.use() loads.
local C = { S = S, listview = listview, CATALOG = "catalog.txt", COINS = "coins.txt", UNITS = "units.slim.json",
  MONEY = "currencies.slim.json" }

local form_view = listview.new(64)
local header_bottom = 0

-- Runs the Lua file at path (relative to the app directory, or under
-- /apps/.lib) with C as its chunk argument; nothing is cached.
function C.use(path)
  local source = fs.read(path)
  if not source then error("Missing " .. path, 0) end
  local chunk = assert(load(source, "@" .. path))
  source = nil
  return chunk(C)
end

-- Numbers and input --------------------------------------------------------------

function C.norm(m, e)
  if m == 0 or m ~= m then return { 0, 0 } end
  local k = math.floor(math.log(math.abs(m), 10))
  m, e = m / 10.0 ^ k, e + k
  if math.abs(m) >= 10 then
    m, e = m / 10, e + 1
  elseif math.abs(m) < 1 then
    m, e = m * 10, e - 1
  end
  return { m, e }
end

-- Parses JSON number text ("4.22211097509778e-102") without passing through a float.
function C.big(text)
  if type(text) ~= "string" then return nil end
  local sign, int, frac, exp = text:match("^%s*([-+]?)(%d*)%.?(%d*)[eE]?([-+]?%d*)%s*$")
  if not sign or int .. frac == "" then return nil end
  local digits = int .. frac
  local nz = digits:find("[1-9]")
  if not nz then return { 0, 0 } end
  local m = tonumber(digits:sub(nz, nz) .. "." .. digits:sub(nz + 1, nz + 8))
  return C.norm(sign == "-" and -m or m, #int - nz + (tonumber(exp) or 0))
end

function C.num(x)
  return x ~= nil and tonumber(x) or nil
end

function C.year_text(y)
  return y < 0 and (-y .. " BC") or tostring(y)
end

function C.parse_year(text)
  local t = text:upper():gsub("%s+", "")
  local n = tonumber(t:match("^%-?%d+"))
  if not n or not t:match("^%-?%d+[ABCDE.]*$") then return nil end
  if t:find("BC") then n = -math.abs(n) end
  return n
end

local function parse_amount(text)
  local t = text:gsub(",", ""):gsub("^%s+", ""):gsub("%s+$", "")
  local w, n, d = t:match("^(%-?%d+)%s+(%d+)/(%d+)$")
  if w then
    local x = math.abs(tonumber(w)) + tonumber(n) / tonumber(d)
    return C.norm(w:sub(1, 1) == "-" and -x or x, 0)
  end
  n, d = t:match("^(%-?[%d.]+)/([%d.]+)$")
  if n and tonumber(n) and tonumber(d) and tonumber(d) ~= 0 then return C.norm(tonumber(n) / tonumber(d), 0) end
  return C.big(t)
end

-- Selections ---------------------------------------------------------------------

local function describe(sel)
  if sel.kind == "unit" then return sel.name .. " (" .. sel.system .. ")" end
  return sel.denom.name .. " · " .. sel.cur.name .. (sel.year and (", " .. C.year_text(sel.year)) or "")
end

function C.compatible(a, b)
  if a.kind ~= b.kind then return false end
  if a.kind == "unit" then return a.category == b.category end
  if a.cur.kind == "n" or b.cur.kind == "n" then return a.cur.id == b.cur.id end
  return true
end

local function convert_now()
  S.result = nil
  if not (S.amount and S.from and S.to) then return end
  local ok, res = pcall(function() return C.use("money.lua")() end)
  if ok then S.result = res else S.message = tostring(res) end
end

local function open_picker(target)
  local ok, err = pcall(function() C.use("pickers.lua").open(target) end)
  if not ok then
    S.picker, S.message = nil, tostring(err)
  end
end

-- Runs after each event, when a finished picker's code is no longer reachable.
local function settle()
  if not S.after_pick then return end
  S.after_pick = false
  convert_now()
  if S.from and not S.to then open_picker("to") end
end

-- Amounts take fractions ("3/4", "1 1/2") and exponents ("6.02E23").
local AMOUNT_KEYS = {
  { "7", "8", "9", "DEL" },
  { "4", "5", "6", "/" },
  { "1", "2", "3", "E" },
  { "±", "0", ".", "SPACE" },
  { "C", "OK", "OK", "OK" },
}

-- Opens the number pad, loaded for as long as it is open.
function C.numpad(opts)
  C.use("/apps/.lib/numpad.lua").open(opts)
end

local function ask_amount()
  C.numpad({
    title = "Amount",
    text = S.amount and S.amount.text or "",
    max_len = 24,
    keys = AMOUNT_KEYS,
    check = function(text)
      if not parse_amount(text) then return "Not a number: " .. text end
    end,
    done = function(text)
      if not text then return end
      S.amount = { text = text, value = parse_amount(text) }
      convert_now()
      if not S.from then open_picker("from") end
    end,
  })
end

local function edit(row)
  if row == 1 then
    ask_amount()
  elseif row == 2 or not S.from then
    open_picker("from")
  else
    open_picker("to")
  end
end

local function swap()
  if S.from and S.to and C.compatible(S.to, S.from) then
    S.from, S.to = S.to, S.from
    convert_now()
  end
end

-- Drawing ----------------------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

local function draw_header(title, right)
  local w = gfx.width()
  local right_w = 0
  if right then
    gfx.font("ui")
    right_w = gfx.text_width(right) + 12
    gfx.text(w - MARGIN - right_w + 12, MARGIN + 4, right)
  end
  gfx.font("ui_large", "bold")
  gfx.text(MARGIN, MARGIN, fit(title, w - 2 * MARGIN - right_w))
  header_bottom = MARGIN + gfx.line_height() + 8
  gfx.line(MARGIN, header_bottom, w - MARGIN, header_bottom, true, 2)
end

local function draw_wrapped(text, x, y, width, max_lines)
  for _, l in ipairs(gfx.wrap(text, width, max_lines or 6)) do
    gfx.text(x, y, l)
    y = y + gfx.line_height()
  end
  return y
end

local function draw_picker(bottom)
  local w = gfx.width()
  local screen = S.picker.stack[#S.picker.stack]
  draw_header(screen.title, S.picker.target == "from" and "From" or "To")
  screen.view:set_bounds(MARGIN, header_bottom + 8, w - 2 * MARGIN, bottom - header_bottom - 8)
  screen.view:draw(#screen.items, function(i, x, y, rw, h, fg)
    local item = screen.items[i]
    local sub_w = 0
    if item.sub then
      gfx.font("small")
      sub_w = gfx.text_width(item.sub) + 16
      gfx.text(x + rw - sub_w + 4, y + (h - gfx.line_height()) // 2, item.sub, fg)
    end
    gfx.font("ui", "bold")
    gfx.text(x + 12, y + (h - gfx.line_height()) // 2, fit(item.label, rw - 24 - sub_w), fg)
  end)
end

local FORM_LABELS = { "Amount", "From", "To" }

local function form_value(i)
  if i == 1 then return S.amount and S.amount.text or "Enter an amount" end
  if i == 2 then return S.from and describe(S.from) or "Choose what it is" end
  return S.to and describe(S.to) or (S.from and "Choose what to convert to" or "-")
end

local function draw_form(bottom)
  local w = gfx.width()
  draw_header("Converter")
  form_view:set_bounds(MARGIN, header_bottom + 8, w - 2 * MARGIN, 3 * 64)
  form_view:draw(3, function(i, x, y, rw, h, fg)
    gfx.font("small")
    gfx.text(x + 12, y + 8, FORM_LABELS[i], fg)
    local lh = gfx.line_height()
    gfx.font("ui", "bold")
    gfx.text(x + 12, y + 10 + lh, fit(form_value(i), rw - 24), fg)
  end)
  local result = S.result
  if not result then return end
  local y = header_bottom + 8 + 3 * 64 + 24
  gfx.line(MARGIN, y - 12, w - MARGIN, y - 12)
  gfx.font("sans18", "bold")
  y = draw_wrapped("= " .. result.main, MARGIN, y, w - 2 * MARGIN, 2)
  gfx.font("sans16")
  y = draw_wrapped(result.label, MARGIN, y, w - 2 * MARGIN, 2) + 8
  gfx.font("ui")
  for _, l in ipairs(result.lines) do
    if y + gfx.line_height() > bottom then break end
    y = draw_wrapped(l, MARGIN, y, w - 2 * MARGIN, 2)
  end
end

local function hint_labels()
  if S.picker then return "Back", "Select", "Prev", "Next" end
  return "Exit", "Change", "Amount", (S.from and S.to and C.compatible(S.to, S.from)) and "Swap" or nil
end

local function draw_app()
  local w, h = gfx.width(), gfx.height()
  local bottom = h - gfx.hints(hint_labels()) - 8
  if S.message then
    gfx.font("ui")
    local msg = gfx.wrap(S.message, w - 2 * MARGIN, 3)
    bottom = bottom - #msg * gfx.line_height()
    draw_wrapped(S.message, MARGIN, bottom, w - 2 * MARGIN, 3)
    bottom = bottom - 8
  end
  if S.picker then
    draw_picker(bottom)
  else
    draw_form(bottom)
  end
end

-- Events ----------------------------------------------------------------------------------

local function close_screen()
  table.remove(S.picker.stack)
  if #S.picker.stack == 0 then S.picker = nil end
end

local function handle_button(button)
  if S.message then
    S.message = nil
    if button == "back" then return true end
  end
  if S.picker then
    local screen = S.picker.stack[#S.picker.stack]
    if button == "back" then
      close_screen()
    elseif button == "up" then
      screen.view:move(-1)
    elseif button == "down" then
      screen.view:move(1)
    elseif button == "left" then
      screen.view:page(-1)
    elseif button == "right" then
      screen.view:page(1)
    elseif button == "confirm" and screen.items[screen.view.selected] then
      screen.items[screen.view.selected].pick()
    end
    return true
  end
  if button == "back" then return false end
  if button == "up" then
    form_view:move(-1)
  elseif button == "down" then
    form_view:move(1)
  elseif button == "confirm" then
    edit(form_view.selected)
  elseif button == "left" then
    ask_amount()
  elseif button == "right" then
    swap()
  end
  return true
end

-- settle() runs after handle_*() returns, so the picker's frame no longer
-- holds its code.
local function on_button_app(button)
  local handled = handle_button(button)
  if handled then
    settle()
    sys.redraw()
  end
  return handled
end

local function handle_touch(x, y)
  S.message = nil
  if S.picker then
    if y < header_bottom then
      close_screen()
    else
      local screen = S.picker.stack[#S.picker.stack]
      local hit, i = screen.view:tap(x, y)
      if hit == "row" then screen.items[i].pick() end
    end
  else
    local hit, i = form_view:tap(x, y)
    if hit == "row" then edit(i) end
  end
end

local function on_touch_app(x, y)
  handle_touch(x, y)
  settle()
  sys.redraw()
end

-- Opens catalog.txt, which must already carry stamp, takes over the app's
-- callbacks and asks for the amount.
return function(stamp)
  S.catalog = C.use("contents.lua")(stamp)
  if not S.catalog then error("Could not read " .. C.CATALOG, 0) end
  draw, on_button, on_touch, on_tick = draw_app, on_button_app, on_touch_app, nil
  ask_amount()
end
