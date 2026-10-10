-- title: Notepad
-- description: notes and checklists
--
-- Each note is one file in the app's notes/ folder, <id>.md:
--   # Title
--   - [ ] open todo
--   - [x] finished todo
--   any other line is a plain text line
--
-- Notes list: Confirm opens, Left deletes (press twice), Right renames.
-- Note: Confirm toggles a todo or edits a text line, Left deletes an item
-- (press twice), Right edits. Touch: tap = Confirm, long-press = Right.

local listview = require("listview")

local NOTES_DIR = "notes"
local MARGIN = 16
local ROW_HEIGHT = 52
local MAX_TITLE = 64
local MAX_ITEM = 256

local notes = {}
local current = nil -- the open note; nil while showing the notes list
local notes_view = listview.new(ROW_HEIGHT)
local note_view = listview.new(ROW_HEIGHT)
local pending_delete = nil -- note or item awaiting a second Delete press
local armed = nil -- pending_delete as it stood when the current input began
local message = nil
local header_bottom = 0

local NOTE_ACTIONS = { "+ Add todo", "+ Add text", "Rename note", "Delete note" }

-- Storage ---------------------------------------------------------------------

local function parse(text)
  local note = { title = "Untitled", items = {} }
  local has_title = false
  for line in (text .. "\n"):gmatch("(.-)\r?\n") do
    local title = line:match("^# (.*)$")
    local mark, rest = line:match("^%- %[([ xX])%] ?(.*)$")
    if title and not has_title then
      note.title, has_title = title, true
    elseif mark then
      note.items[#note.items + 1] = { todo = true, done = mark ~= " ", text = rest }
    elseif line ~= "" then
      note.items[#note.items + 1] = { todo = false, text = line }
    end
  end
  return note
end

local function save(note)
  local lines = { "# " .. note.title }
  for _, item in ipairs(note.items) do
    if item.todo then
      lines[#lines + 1] = "- [" .. (item.done and "x" or " ") .. "] " .. item.text
    else
      lines[#lines + 1] = item.text
    end
  end
  local ok, err = fs.write(note.file, table.concat(lines, "\n") .. "\n")
  if not ok then message = "Save failed: " .. tostring(err) end
end

local function load_notes()
  notes = {}
  for _, name in ipairs(fs.list(NOTES_DIR) or {}) do
    local id = name:match("^(%d+)%.md$")
    local text = id and fs.read(NOTES_DIR .. "/" .. name)
    if text then
      local note = parse(text)
      note.id, note.file = tonumber(id), NOTES_DIR .. "/" .. name
      notes[#notes + 1] = note
    end
  end
  table.sort(notes, function(a, b) return a.id > b.id end) -- newest first
end

local function create_note(title)
  local id = 1
  for _, note in ipairs(notes) do id = math.max(id, note.id + 1) end
  local note = { id = id, file = NOTES_DIR .. "/" .. id .. ".md", title = title, items = {} }
  table.insert(notes, 1, note)
  save(note)
  return note
end

local function delete_note(note)
  for i, n in ipairs(notes) do
    if n == note then table.remove(notes, i) break end
  end
  fs.remove(note.file)
end

local function todo_counts(note)
  local done, total = 0, 0
  for _, item in ipairs(note.items) do
    if item.todo then
      total = total + 1
      if item.done then done = done + 1 end
    end
  end
  return done, total
end

-- Actions ---------------------------------------------------------------------

local function open_note(note)
  current = note
  note_view.selected, note_view.top = 1, 1
  note_view:clamp(#note.items + #NOTE_ACTIONS)
end

local function edit_text(title, initial, max_len, on_done)
  sys.keyboard(title, initial, function(text)
    if text then on_done(text) end
    sys.redraw()
  end, max_len)
end

-- Two-step delete: the first press arms it, a second press on the same target confirms.
local function confirm_delete(target, what)
  if armed == target then return true end
  pending_delete = target
  message = "Press Delete again to remove " .. what
  return false
end

local function list_activate(row)
  if row == 1 then
    edit_text("Note title", "", MAX_TITLE, function(title)
      if title ~= "" then open_note(create_note(title)) end
    end)
  elseif notes[row - 1] then
    open_note(notes[row - 1])
  end
end

local function list_delete(row)
  local note = notes[row - 1]
  if note and confirm_delete(note, "\"" .. note.title .. "\"") then delete_note(note) end
end

local function list_rename(row)
  local note = notes[row - 1]
  if not note then return end
  edit_text("Rename note", note.title, MAX_TITLE, function(title)
    if title ~= "" then
      note.title = title
      save(note)
    end
  end)
end

local function edit_item(index)
  local item = current.items[index]
  if not item then return end
  local note = current
  edit_text(item.todo and "Edit todo" or "Edit text", item.text, MAX_ITEM, function(text)
    if text == "" then
      table.remove(note.items, index)
    else
      item.text = text
    end
    save(note)
  end)
end

local function add_item(todo)
  local note = current
  edit_text(todo and "New todo" or "New text", "", MAX_ITEM, function(text)
    if text ~= "" then
      note.items[#note.items + 1] = { todo = todo, done = false, text = text }
      save(note)
    end
  end)
end

-- Renaming from inside a note reuses the list action on the note's row.
local function rename_current()
  for i, note in ipairs(notes) do
    if note == current then return list_rename(i + 1) end
  end
end

local function note_activate(row)
  local items = current.items
  if row <= #items then
    local item = items[row]
    if item.todo then
      item.done = not item.done
      save(current)
    else
      edit_item(row)
    end
    return
  end
  local action = row - #items
  if action == 1 then
    add_item(true)
  elseif action == 2 then
    add_item(false)
  elseif action == 3 then
    rename_current()
  elseif action == 4 then
    if confirm_delete(current, "this note") then
      delete_note(current)
      current = nil
    end
  end
end

local function note_delete(row)
  local item = current.items[row]
  if item and confirm_delete(item, "this item") then
    table.remove(current.items, row)
    save(current)
  end
end

local function activate(row)
  if current then
    note_activate(row)
  else
    list_activate(row)
  end
end

local function secondary(row)
  if current then edit_item(row) else list_rename(row) end
end

-- Drawing ---------------------------------------------------------------------

local function fit(text, width)
  return gfx.wrap(text, width, 1)[1] or ""
end

local function centered_text(x, y, w, h, text, fg)
  local lh = gfx.line_height()
  gfx.text(x, y + (h - lh) // 2, fit(text, w), fg)
end

local function draw_checkbox(x, y, done, fg)
  local size = 22
  gfx.rect(x, y, size, size, fg)
  gfx.rect(x + 1, y + 1, size - 2, size - 2, fg)
  if done then
    gfx.line(x + 5, y + 11, x + 9, y + 16, fg, 3)
    gfx.line(x + 9, y + 16, x + 17, y + 5, fg, 3)
  end
  return size
end

local function draw_note_row(i, x, y, w, h, fg)
  gfx.font("ui")
  if i == 1 then
    centered_text(x + 12, y, w - 24, h, "+ New note", fg)
    return
  end
  local note = notes[i - 1]
  local done, total = todo_counts(note)
  local right = x + w - 12
  if total > 0 then
    local count = done .. "/" .. total
    right = right - gfx.text_width(count)
    centered_text(right, y, gfx.text_width(count), h, count, fg)
    right = right - 12
  end
  gfx.font("ui", "bold")
  centered_text(x + 12, y, right - x - 12, h, note.title, fg)
end

local function draw_item_row(i, x, y, w, h, fg)
  gfx.font("ui")
  local items = current.items
  if i > #items then
    centered_text(x + 12, y, w - 24, h, NOTE_ACTIONS[i - #items], fg)
    return
  end
  local item = items[i]
  local tx = x + 12
  if item.todo then
    tx = tx + draw_checkbox(tx, y + (h - 22) // 2, item.done, fg) + 12
  end
  local text = fit(item.text, x + w - 12 - tx)
  local ty = y + (h - gfx.line_height()) // 2
  gfx.text(tx, ty, text, fg)
  if item.todo and item.done then
    local mid = ty + gfx.line_height() // 2
    gfx.line(tx, mid, tx + gfx.text_width(text), mid, fg)
  end
end

local function hint_labels()
  if not current then
    local on_note = notes_view.selected > 1
    return "Exit", notes_view.selected == 1 and "New" or "Open", on_note and "Delete" or nil, on_note and "Rename" or nil
  end
  local item = current.items[note_view.selected]
  if not item then return "Notes", "Select", nil, nil end
  return "Notes", item.todo and "Toggle" or "Edit", "Delete", "Edit"
end

function draw()
  local w, h = gfx.width(), gfx.height()
  local hints_height = gfx.hints(hint_labels())

  gfx.font("ui_large", "bold")
  local title = current and current.title or "Notepad"
  gfx.text(MARGIN, MARGIN, fit(title, w - 2 * MARGIN))
  header_bottom = MARGIN + gfx.line_height() + 8
  gfx.line(MARGIN, header_bottom, w - MARGIN, header_bottom, true, 2)

  local bottom = h - hints_height - 8
  if message then
    gfx.font("ui")
    bottom = bottom - gfx.line_height()
    gfx.text(MARGIN, bottom, fit(message, w - 2 * MARGIN))
    bottom = bottom - 8
  end

  local top = header_bottom + 8
  if current then
    note_view:set_bounds(MARGIN, top, w - 2 * MARGIN, bottom - top)
    note_view:draw(#current.items + #NOTE_ACTIONS, draw_item_row)
  else
    notes_view:set_bounds(MARGIN, top, w - 2 * MARGIN, bottom - top)
    notes_view:draw(#notes + 1, draw_note_row)
  end
end

-- Input -----------------------------------------------------------------------

local function clear_pending()
  pending_delete, message = nil, nil
end

-- Any input disarms a pending delete unless that input confirms it.
local function begin_input()
  armed = pending_delete
  clear_pending()
end

function on_button(button)
  if button == "back" then
    if pending_delete or message then
      clear_pending()
    elseif current then
      current = nil
    else
      return false -- leave the app
    end
    sys.redraw()
    return true
  end

  local view = current and note_view or notes_view
  begin_input()
  if button == "up" then
    view:move(-1)
  elseif button == "down" then
    view:move(1)
  elseif button == "confirm" then
    activate(view.selected)
  elseif button == "left" then
    if current then note_delete(view.selected) else list_delete(view.selected) end
  elseif button == "right" then
    secondary(view.selected)
  end
  sys.redraw()
end

function on_touch(x, y, kind)
  begin_input()
  if current and y < header_bottom then
    current = nil -- tapping the title returns to the notes list
    sys.redraw()
    return
  end
  local view = current and note_view or notes_view
  local hit, row = view:tap(x, y)
  if hit == "row" then
    if kind == "long" then secondary(row) else activate(row) end
  end
  sys.redraw()
end

fs.mkdir(NOTES_DIR)
load_notes()
