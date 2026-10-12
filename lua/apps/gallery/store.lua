-- Folder scanning, the .pxc image cache and settings for the gallery.
--
-- Cache files are named by a hash of the source path, its size and the box the
-- image was fitted into, so an edited or replaced image (new size) or a new
-- orientation (new box) gets a fresh cache and old entries are simply unused.

local store = {}

local ROOT = sys.app_dir()
local DATA = ROOT .. "/.data"
local CACHE = DATA .. "/cache"
local SETTINGS = DATA .. "/settings.txt"

store.ROOT = ROOT

local IMAGE_EXT = { jpg = true, jpeg = true, png = true, bmp = true }
-- Photos listed per folder; more would not fit the app's Lua heap.
store.MAX_FOLDER_IMAGES = 500

local function ext(name)
  return (name:match("%.(%w+)$") or ""):lower()
end

function store.is_image(name)
  return IMAGE_EXT[ext(name)] == true
end

-- Case-insensitive sort key with digit runs zero-padded, so "p2" sorts
-- before "p10".
local function sort_key(name)
  return (name:lower():gsub("%d+", function(digits) return string.rep("0", 10 - #digits) .. digits end))
end

-- Sorts names in place by sort_key. Sorting "key\0name" strings needs no
-- comparator calls back into Lua.
local function sort_names(names)
  for i, name in ipairs(names) do names[i] = sort_key(name) .. "\0" .. name end
  table.sort(names)
  for i, keyed in ipairs(names) do names[i] = keyed:sub(keyed:find("\0", 1, true) + 1) end
  return names
end

-- Returns the names of the folders and images directly inside dir, each
-- sorted. Names starting with "." are hidden; images past MAX_FOLDER_IMAGES
-- (in card order) are left out.
function store.scan(dir)
  local dirs, images = {}, {}
  for _, name in ipairs(fs.list(dir) or {}) do
    if name:sub(1, 1) ~= "." then
      local folder = name:match("^(.*)/$")
      if folder then
        dirs[#dirs + 1] = folder
      elseif store.is_image(name) and #images < store.MAX_FOLDER_IMAGES then
        images[#images + 1] = name
      end
    end
  end
  return sort_names(dirs), sort_names(images)
end

-- Photos in dir and its subfolders (three levels deep), as paths relative to
-- dir in browsing order, stopping at limit.
function store.collect(dir, limit)
  local out = {}
  local function walk(prefix, depth)
    local dirs, images = store.scan(prefix == "" and dir or dir .. "/" .. prefix)
    local lead = prefix == "" and "" or prefix .. "/"
    for _, name in ipairs(images) do
      if #out >= limit then return end
      out[#out + 1] = lead .. name
    end
    images = nil
    if depth > 0 then
      for _, name in ipairs(dirs) do
        if #out >= limit then return end
        walk(lead .. name, depth - 1)
      end
    end
  end
  walk("", 3)
  return out
end

-- Settings -------------------------------------------------------------------

store.settings = { interval = 300, shuffle = false, subfolders = false, low_power = true }

function store.load_settings()
  local text = fs.read(SETTINGS)
  if not text then return end
  local s = store.settings
  for key, value in text:gmatch("(%w+)=(%w+)") do
    if key == "interval" then
      s.interval = math.max(10, tonumber(value) or s.interval)
    elseif s[key] ~= nil then
      s[key] = value == "1"
    end
  end
end

function store.save_settings()
  local s = store.settings
  fs.mkdir(DATA)
  fs.write(SETTINGS, string.format("interval=%d\nshuffle=%d\nsubfolders=%d\nlow_power=%d\n", s.interval,
    s.shuffle and 1 or 0, s.subfolders and 1 or 0, s.low_power and 1 or 0))
end

-- Cache ----------------------------------------------------------------------

-- 32-bit FNV-1a; the mask keeps the 64-bit simulator's integers in range.
local function hash(text)
  local h = 0x811c9dc5
  for i = 1, #text do
    h = ((h ~ text:byte(i)) * 16777619) & 0xffffffff
  end
  return h
end

-- Largest size with the image's aspect ratio inside box_w x box_h; only
-- shrinks when grow is false.
local function fit(w, h, box_w, box_h, grow)
  if not grow and w <= box_w and h <= box_h then return w, h end
  if w * box_h > h * box_w then
    return box_w, math.max(1, h * box_w // w)
  end
  return math.max(1, w * box_h // h), box_h
end

local function cache_path(path, box_w, box_h)
  local key = path .. "|" .. (fs.size(path) or 0) .. "|" .. box_w .. "x" .. box_h
  return string.format("%s/%08x.pxc", CACHE, hash(key))
end

-- Returns the file to draw for path fitted into box_w x box_h and its size,
-- or nil when a JPEG or PNG has not been cached for this box yet (or nil, err
-- when a BMP cannot be read). BMPs draw directly; they are never cached.
function store.lookup(path, box_w, box_h)
  if ext(path) == "bmp" then
    local w, h = gfx.image_size(path)
    if not w then return nil, h end
    w, h = fit(w, h, box_w, box_h, false)
    return path, w, h
  end
  local dst = cache_path(path, box_w, box_h)
  local w, h = gfx.image_size(dst)
  if w then return dst, w, h end
  return nil
end

-- Like lookup, but decodes and caches a JPEG or PNG first when needed (a
-- second or two for a camera photo). Returns nil, err on failure.
function store.prepare(path, box_w, box_h)
  local file, w, h = store.lookup(path, box_w, box_h)
  if file or ext(path) == "bmp" then return file, w, h end
  local src_w, src_h = gfx.image_size(path)
  if not src_w then return nil, src_h end
  w, h = fit(src_w, src_h, box_w, box_h, true)
  local dst = cache_path(path, box_w, box_h)
  fs.mkdir(DATA)
  fs.mkdir(CACHE)
  fs.remove(dst) -- a corrupt leftover
  local ok, err = gfx.cache(path, dst, w, h)
  if not ok then return nil, err end
  return dst, w, h
end

-- Deletes every cached image; returns how many were removed.
function store.clear_cache()
  local removed = 0
  for _, name in ipairs(fs.list(CACHE) or {}) do
    if fs.remove(CACHE .. "/" .. name) then removed = removed + 1 end
  end
  return removed
end

return store
