-- Article links in pack text (format 3): "\6" .. text .. "\7" .. 3 id bytes,
-- each byte 0x30 + 6 bits of the target id, most significant first.
--
--   local plain, spans = links.parse(text)   -- spans: { from, to, id } byte ranges in plain, or nil
--   links.attach(lines, plain, spans)        -- after gfx.wrap with the line's font set

local links = {}

function links.parse(text)
  if not text:find("\6", 1, true) then return text, nil end
  local out, spans = {}, {}
  local pos, len = 1, 0
  while true do
    local open = text:find("\6", pos, true)
    local close = open and text:find("\7", open + 1, true)
    if not close or close + 3 > #text then break end
    out[#out + 1] = text:sub(pos, open - 1)
    len = len + open - pos
    local anchor = text:sub(open + 1, close - 1)
    local b1, b2, b3 = text:byte(close + 1, close + 3)
    spans[#spans + 1] = { from = len + 1, to = len + #anchor, id = ((b1 - 48) << 12) | ((b2 - 48) << 6) | (b3 - 48) }
    out[#out + 1] = anchor
    len = len + #anchor
    pos = close + 4
  end
  out[#out + 1] = text:sub(pos)
  return table.concat(out), spans
end

-- Gives each wrapped line the link segments it shows: line.links = { { x0, x1,
-- text, id, span } }. gfx.wrap only breaks between words, so each line is found
-- by searching forward in plain.
function links.attach(lines, plain, spans)
  local cursor, first = 1, 1
  for _, line in ipairs(lines) do
    local s = plain:find(line.text, cursor, true)
    if s then
      local e = s + #line.text - 1
      cursor = e + 1
      while spans[first] and spans[first].to < s do first = first + 1 end
      local j = first
      while spans[j] and spans[j].from <= e do
        local span = spans[j]
        local a, b = math.max(span.from, s), math.min(span.to, e)
        if a <= b then
          local text = plain:sub(a, b)
          -- Measured from the end: text_width ignores a trailing space before the link.
          local x1 = line.x + gfx.text_width(plain:sub(s, b))
          line.links = line.links or {}
          line.links[#line.links + 1] = { x0 = x1 - gfx.text_width(text), x1 = x1, text = text, id = span.id, span = span }
        end
        j = j + 1
      end
    end
  end
end

-- The distinct links on a page, in reading order; sets seg.n on each segment.
function links.on_page(lines)
  local list, index = {}, {}
  for _, line in ipairs(lines) do
    for _, seg in ipairs(line.links or {}) do
      local n = index[seg.span]
      if not n then
        list[#list + 1] = seg.id
        n = #list
        index[seg.span] = n
      end
      seg.n = n
    end
  end
  return list
end

return links
