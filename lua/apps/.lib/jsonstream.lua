-- Pull parser for JSON files too large to load whole. It reads 4KB chunks
-- through fs.read and decodes one value at a time, so only the parts a caller
-- keeps are held in memory, and skipped values cost no more than the buffer.
--
--   local jsonstream = require("jsonstream")
--   local r = jsonstream.open("data.json")       -- or open(path, byte_offset)
--   r:object(function(key)
--     if key == "items" then
--       r:array(function(i) local at = r:offset(); local item = r:value() end)
--     else
--       r:skip()
--     end
--   end)
--
-- Every callback must consume exactly one value (r:value(), r:skip(),
-- r:string(), r:object(...), ...). Numbers come back as their source text:
-- tonumber() them, or parse the text yourself when 32-bit floats would lose
-- range. null decodes to nil. Malformed input raises an error.

local jsonstream = {}
jsonstream.__index = jsonstream

local CHUNK = 4096
local ESCAPES = { b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }

-- Opens path for parsing from byte offset (default 0); nil, err if unreadable.
function jsonstream.open(path, offset)
  local size, err = fs.size(path)
  if not size then return nil, err end
  offset = offset or 0
  return setmetatable({ path = path, size = size, buf = "", pos = 1, base = offset, next_read = offset }, jsonstream)
end

function jsonstream:fail(what)
  error(string.format("json: %s at byte %d of %s", what, self.base + self.pos - 1, self.path), 0)
end

-- Appends the next chunk, dropping everything before pos.
local function more(self)
  if self.next_read >= self.size then return false end
  local chunk = fs.read(self.path, self.next_read, CHUNK)
  if not chunk or chunk == "" then return false end
  self.base = self.base + self.pos - 1
  self.buf = self.buf:sub(self.pos) .. chunk
  self.pos = 1
  self.next_read = self.next_read + #chunk
  return true
end

-- Finds a single-character pattern at or after pos + rel, reading more as
-- needed; returns its index in buf or nil at end of file.
local function find(self, pattern, rel)
  rel = rel or 0
  while true do
    local s = self.buf:find(pattern, self.pos + rel)
    if s then return s end
    rel = math.max(rel, #self.buf - self.pos + 1)
    if not more(self) then return nil end
  end
end

-- Skips whitespace; returns the next byte without consuming it.
local function peek(self)
  local s = find(self, "[^ \t\r\n]")
  if not s then self:fail("unexpected end") end
  self.pos = s
  return self.buf:byte(s)
end

local function expect(self, byte, what)
  if peek(self) ~= byte then self:fail("expected " .. what) end
  self.pos = self.pos + 1
end

-- Byte offset of the next value, for reopening the file there later.
function jsonstream:offset()
  peek(self)
  return self.base + self.pos - 1
end

local function unescape(raw)
  raw = raw:gsub("\\u(%x%x%x%x)", function(hex)
    local code = tonumber(hex, 16)
    return (code >= 0xD800 and code <= 0xDFFF) and "?" or utf8.char(code)
  end)
  return (raw:gsub("\\(.)", function(c) return ESCAPES[c] or c end))
end

function jsonstream:string()
  expect(self, 34, "string")
  local rel, escaped = 0, false
  while true do
    local s = find(self, '["\\]', rel)
    if not s then self:fail("unterminated string") end
    if self.buf:byte(s) == 34 then
      local raw = self.buf:sub(self.pos, s - 1)
      self.pos = s + 1
      return escaped and unescape(raw) or raw
    end
    escaped = true
    rel = s - self.pos + 2 -- past the escaped character
  end
end

-- A number's source text, e.g. "4.22211097509778e-102".
function jsonstream:number()
  peek(self)
  local s = find(self, "[^%-%d.eE+]", 1)
  local stop = s and s - 1 or #self.buf
  local text = self.buf:sub(self.pos, stop)
  if text == "" then self:fail("expected a value") end
  self.pos = stop + 1
  return text
end

local function literal(self)
  local s = find(self, "[^%a]", 1)
  local stop = s and s - 1 or #self.buf
  local word = self.buf:sub(self.pos, stop)
  self.pos = stop + 1
  if word == "true" then return true elseif word == "false" then return false elseif word == "null" then return nil end
  self:fail("unknown literal " .. word)
end

-- Calls fn(key) for each member; fn must consume the member's value.
function jsonstream:object(fn)
  expect(self, 123, "object")
  if peek(self) == 125 then
    self.pos = self.pos + 1
    return
  end
  while true do
    local key = self:string()
    expect(self, 58, "':'")
    fn(key)
    local c = peek(self)
    self.pos = self.pos + 1
    if c == 125 then return end
    if c ~= 44 then self:fail("expected ',' or '}'") end
  end
end

-- Calls fn(index) for each element; fn must consume the element.
function jsonstream:array(fn)
  expect(self, 91, "array")
  if peek(self) == 93 then
    self.pos = self.pos + 1
    return
  end
  local i = 0
  while true do
    i = i + 1
    fn(i)
    local c = peek(self)
    self.pos = self.pos + 1
    if c == 93 then return end
    if c ~= 44 then self:fail("expected ',' or ']'") end
  end
end

-- Decodes the next value into Lua values (keep this to small values).
function jsonstream:value()
  local c = peek(self)
  if c == 123 then
    local t = {}
    self:object(function(key) t[key] = self:value() end)
    return t
  elseif c == 91 then
    local t = {}
    self:array(function(i) t[i] = self:value() end)
    return t
  elseif c == 34 then
    return self:string()
  elseif c == 116 or c == 102 or c == 110 then
    return literal(self)
  end
  return self:number()
end

-- Consumes the next value without building it.
function jsonstream:skip()
  local c = peek(self)
  if c == 34 then
    self:string()
  elseif c == 123 or c == 91 then
    local depth = 0
    while true do
      local s = find(self, '[%[%]{}"]')
      if not s then self:fail("unterminated value") end
      local b = self.buf:byte(s)
      self.pos = s
      if b == 34 then
        self:string()
      else
        self.pos = s + 1
        depth = depth + ((b == 123 or b == 91) and 1 or -1)
        if depth == 0 then return end
      end
    end
  elseif c == 116 or c == 102 or c == 110 then
    literal(self)
  else
    self:number()
  end
end

return jsonstream
