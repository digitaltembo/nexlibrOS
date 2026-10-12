-- Indexes catalog.txt (format in catalog.lua) once at start;
-- loaded by ui.lua's C.use() and then released. Chunk argument: ui.lua's
-- shared table C. Returns load_index(stamp).
--
-- Each kind's systems, and the money regions, are packed as lines of
-- "<offset>|<length>|<rows>|<priced rows>|<name>" giving the byte range of the
-- section's rows in catalog.txt, so only names stay in memory.

local C = ...
local lines = C.use("/apps/.lib/lines.lua")

return function(stamp)
  local index = { kinds = {}, groups = "", money_count = 0 }
  local kind, packed, state, offset = nil, {}, "header", 0
  local section -- { name, offset, length, count, priced, kind }
  local function close_section()
    if not section then return end
    local line = string.format("%d|%d|%d|%d|%s", section.offset, section.length, section.count, section.priced,
      section.name)
    if section.kind then
      section.kind.count = section.kind.count + section.count
      packed[#packed + 1] = line
    else
      index.money_count = index.money_count + section.count
      index.groups = index.groups .. line .. "\n"
    end
    section = nil
  end
  local function close_kind()
    close_section()
    if kind then kind.systems = table.concat(packed, "\n") end
    kind, packed = nil, {}
  end
  lines.each(C.CATALOG, function(line)
    local next_offset = offset + #line + 1
    if state == "header" then
      state = line:gsub("%s+$", "") == stamp and "body" or "stale"
    elseif state == "body" then
      local tag, rest = line:match("^(%u)|(.*)$")
      if tag == "K" then
        close_kind()
        local key, name, base = rest:match("^([^|]*)|([^|]*)|(.*)$")
        kind = { key = key, name = name, base = base, count = 0 }
        index.kinds[#index.kinds + 1] = kind
      elseif tag == "S" or tag == "G" then
        if tag == "G" then close_kind() else close_section() end
        section = { name = rest, offset = next_offset, length = 0, count = 0, priced = 0, kind = kind }
      elseif section and line ~= "" then
        section.length = section.length + #line + 1
        section.count = section.count + 1
        if line:sub(-1) ~= "n" or section.kind then section.priced = section.priced + 1 end
        if not index.usd and not section.kind and line:find("^%d+|usd|") then index.usd = line end
      end
    end
    offset = next_offset
  end)
  close_kind()
  if state ~= "body" or #index.kinds == 0 or index.groups == "" then return nil end
  return index
end
