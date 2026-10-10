-- Streams a file line by line in fixed-size chunks, so files larger than
-- fs.read's single-read limit (and larger than is sensible to hold in RAM)
-- can be processed.
--
--   local lines = require("lines")
--   lines.each("log.txt", function(line) ... end)   -- false if unreadable

local lines = {}

local CHUNK = 4096

-- Calls fn(line) for each line (without its "\r\n" / "\n"). Returns false when
-- the file cannot be opened.
function lines.each(path, fn)
  local size = fs.size(path)
  if not size then return false end
  local offset, carry = 0, ""
  while offset < size do
    local chunk = fs.read(path, offset, CHUNK)
    if not chunk or chunk == "" then break end
    offset = offset + #chunk
    chunk = carry .. chunk
    local start = 1
    while true do
      local nl = chunk:find("\n", start, true)
      if not nl then break end
      fn((chunk:sub(start, nl - 1):gsub("\r$", "")))
      start = nl + 1
    end
    carry = chunk:sub(start)
  end
  if carry ~= "" then fn((carry:gsub("\r$", ""))) end
  return true
end

return lines
