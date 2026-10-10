-- Scrolling, selectable list. Buttons move the selection; taps hit rows; when
-- the rows overflow, arrow buttons at the right edge page the list for touch.
--
--   local listview = require("listview")
--   local list = listview.new(52)
--   list:set_bounds(x, y, w, h)              -- in draw(), before list:draw
--   list:draw(count, function(i, x, y, w, h, fg) ... end)
--   list:move(1)                             -- on_button("down")
--   local kind, i = list:tap(x, y)           -- on_touch: "row", i | "scroll" | nil

local listview = {}
listview.__index = listview

local ARROW = 40

function listview.new(row_height)
  return setmetatable({ selected = 1, top = 1, count = 0, row_h = row_height or 48, x = 0, y = 0, w = 0, h = 0 },
    listview)
end

function listview:set_bounds(x, y, w, h)
  self.x, self.y, self.w, self.h = x, y, w, h
end

function listview:visible()
  return math.max(1, self.h // self.row_h)
end

function listview:overflows()
  return self.count > self:visible()
end

function listview:row_width()
  return self.w - (self:overflows() and ARROW + 8 or 0)
end

-- Clamps the selection to count rows and scrolls it into view.
function listview:clamp(count)
  self.count = count
  local vis = self:visible()
  self.selected = math.max(1, math.min(self.selected, count))
  if self.selected < self.top then self.top = self.selected end
  if self.selected >= self.top + vis then self.top = self.selected - vis + 1 end
  self.top = math.max(1, math.min(self.top, count - vis + 1))
end

function listview:select(index)
  self.selected = index
  self:clamp(self.count)
end

-- Moves the selection by delta rows, wrapping at either end.
function listview:move(delta)
  if self.count == 0 then return end
  self.selected = (self.selected - 1 + delta) % self.count + 1
  self:clamp(self.count)
end

-- Scrolls a page up (-1) or down (1) and selects the first visible row.
function listview:page(direction)
  local vis = self:visible()
  self.top = math.max(1, math.min(self.top + direction * vis, self.count - vis + 1))
  self.selected = self.top
  self:clamp(self.count)
end

local function arrow_rects(self)
  local ax = self.x + self.w - ARROW
  return ax, self.y, ax, self.y + self.h - ARROW
end

local function draw_arrow(x, y, up, enabled)
  local cx, cy = x + ARROW // 2, y + ARROW // 2
  local points = up and { cx - 12, cy + 6, cx + 12, cy + 6, cx, cy - 8 }
      or { cx - 12, cy - 6, cx + 12, cy - 6, cx, cy + 8 }
  if enabled then gfx.fill_polygon(points) else gfx.polygon(points) end
end

-- draw_row(index, x, y, w, h, fg): fg is the ink colour for the row (false,
-- i.e. white, on the selected row's dark highlight).
function listview:draw(count, draw_row)
  self:clamp(count)
  local w = self:row_width()
  local last = math.min(count, self.top + self:visible() - 1)
  for i = self.top, last do
    local ry = self.y + (i - self.top) * self.row_h
    local selected = i == self.selected
    if selected then gfx.fill_round_rect(self.x, ry + 2, w, self.row_h - 4, 6) end
    draw_row(i, self.x, ry, w, self.row_h, not selected)
  end
  if self:overflows() then
    local ux, uy, dx, dy = arrow_rects(self)
    draw_arrow(ux, uy, true, self.top > 1)
    draw_arrow(dx, dy, false, last < count)
  end
end

-- Returns "row", index for a tap on a row (and selects it), "scroll" for a
-- tap on a paging arrow, or nil.
function listview:tap(x, y)
  if self:overflows() and x >= self.x + self.w - ARROW and x < self.x + self.w then
    local _, uy, _, dy = arrow_rects(self)
    if y >= uy and y < uy + ARROW then self:page(-1) return "scroll" end
    if y >= dy and y < dy + ARROW then self:page(1) return "scroll" end
  end
  if x < self.x or x >= self.x + self:row_width() or y < self.y or y >= self.y + self.h then return nil end
  local i = self.top + (y - self.y) // self.row_h
  if i > math.min(self.count, self.top + self:visible() - 1) then return nil end
  self.selected = i
  return "row", i
end

return listview
