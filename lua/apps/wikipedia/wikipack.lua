-- Reader for packs written by scripts/wikipedia/build_wiki_pack.py (format 3).
--
--   local wikipack = require("wikipack")
--   local pack, err = wikipack.open("data")
--   pack:search("einstein", 20)  -> { { id, title, via, exact }, ... }  (via: matched redirect key or nil)
--   pack:size(id), pack:title(id)
--   pack:read(id, skip, len)    -> bytes [skip, skip+len) of the article text (len <= 16KB)
--   pack:random()               -> id of a random article with text

local wikipack = {}
wikipack.__index = wikipack

local FORMAT_VERSION = 3

-- Search key for a typed query, matching norm_key() in the builder for ASCII
-- input (the on-screen keyboard's): lowercase, and every run of ASCII
-- non-alphanumerics becomes one space. Other bytes pass through unchanged.
function wikipack.key(text)
  return (text:lower():gsub("[%c%p%s]+", " "):gsub("^ ", ""):gsub(" $", "")):sub(1, 255)
end

function wikipack.open(root)
  local text, err = fs.read(root .. "/wiki.hdr")
  if not text then return nil, err end
  local hdr = {}
  for k, v in text:gmatch("([%w_]+)=([^\n]*)") do hdr[k] = tonumber(v) or v end
  if hdr.version ~= FORMAT_VERSION then return nil, "unsupported pack version " .. tostring(hdr.version) end
  return setmetatable({ root = root, hdr = hdr, last_id = nil, last_rec = nil }, wikipack)
end

function wikipack:count()
  return self.hdr.articles
end

-- block, inner offset, text length, titles.bin offset
function wikipack:record(id)
  if self.last_id ~= id then
    local raw = fs.read(self.root .. "/articles.bin", id * 16, 16)
    if not raw or #raw < 16 then error("bad article id " .. tostring(id)) end
    self.last_rec = { string.unpack("<I4I4I4I4", raw) }
    self.last_id = id
  end
  return self.last_rec
end

function wikipack:size(id)
  return self:record(id)[3]
end

function wikipack:title(id)
  local off = self:record(id)[4]
  local raw = fs.read(self.root .. "/titles.bin", off, 256) or ""
  local n = raw:byte(1) or 0
  return raw:sub(2, 1 + n)
end

function wikipack:read(id, skip, len)
  local rec = self:record(id)
  len = math.min(len, rec[3] - skip)
  if len <= 0 then return "" end
  local braw = self.hdr.block_raw
  local pos = rec[2] + skip
  local block, inner = rec[1] + pos // braw, pos % braw
  local parts = {}
  local got = 0
  while got < len do
    local lo, hi = string.unpack("<I4I4", fs.read(self.root .. "/blocks.bin", block * 4, 8))
    local want = math.min(len - got, braw - inner)
    local part, err = fs.inflate(self.root .. "/text.bin", lo, hi - lo, inner, want)
    if not part then return nil, err end
    if #part == 0 then break end
    parts[#parts + 1] = part
    got = got + #part
    block, inner = block + 1, 0
  end
  return table.concat(parts)
end

function wikipack:random()
  for _ = 1, 64 do
    local id = math.random(0, self.hdr.articles - 1)
    if self:size(id) > 0 then return id end
  end
  return 0
end

-- Index ------------------------------------------------------------------------

function wikipack:page_first(i)
  local p = self.hdr.key_prefix
  return (fs.read(self.root .. "/pages.bin", i * p, p) or ""):gsub("%z+$", "")
end

-- Calls fn(key, id, redirect) for each entry of index page i until fn returns true.
function wikipack:each_entry(i, fn)
  local page = fs.read(self.root .. "/index.bin", i * self.hdr.page_bytes, self.hdr.page_bytes) or ""
  local pos, key = 1, ""
  while pos + 1 <= #page do
    local shared, n = page:byte(pos, pos + 1)
    if n == 0 then return end
    key = key:sub(1, shared) .. page:sub(pos + 2, pos + 1 + n)
    pos = pos + 2 + n
    local v, shift = 0, 0
    repeat
      local b = page:byte(pos)
      pos = pos + 1
      v = v | ((b & 0x7F) << shift)
      shift = shift + 7
    until b < 0x80
    if fn(key, v >> 1, v & 1 == 1) then return true end
  end
end

-- Titles starting with query: exact matches first, then by popularity (lower id).
function wikipack:search(query, limit, scan_pages)
  local q = wikipack.key(query)
  if q == "" then return {} end
  local qp = q:sub(1, self.hdr.key_prefix)
  local lo, hi = 0, self.hdr.pages - 1
  while lo < hi do -- last page whose first key < qp, or page 0
    local mid = (lo + hi + 1) // 2
    if self:page_first(mid) < qp then lo = mid else hi = mid - 1 end
  end
  limit = limit or 20
  -- Only the best `limit` hits are kept, ranked as they arrive: a short prefix
  -- matches hundreds of titles and redirects, more than the app heap holds.
  -- An evicted id can only come back ranked higher (as an exact match).
  local best = {}
  local function before(exact, id, hit)
    if exact ~= hit.exact then return exact end
    return id < hit.id
  end
  local function add(id, exact, via)
    local at = #best + 1
    for i, hit in ipairs(best) do
      if hit.id == id then
        if not exact or hit.exact then return end
        table.remove(best, i)
        at = #best + 1
        break
      end
    end
    while at > 1 and before(exact, id, best[at - 1]) do at = at - 1 end
    if at > limit then return end
    table.insert(best, at, { id = id, exact = exact, via = via })
    best[limit + 1] = nil
  end
  for p = lo, math.min(lo + (scan_pages or 3), self.hdr.pages) - 1 do
    local past = self:each_entry(p, function(key, id, redirect)
      if key:sub(1, #q) == q then
        local exact = key == q
        add(id, exact, redirect and key or nil)
      elseif key > q then
        return true
      end
    end)
    if past then break end
  end
  local out = {}
  for i, hit in ipairs(best) do
    out[i] = { id = hit.id, title = self:title(hit.id), via = hit.via, exact = hit.exact }
  end
  return out
end

return wikipack
