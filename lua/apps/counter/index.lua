-- title: Counter
-- description: Press left/right to count
local n = tonumber(fs.read("count.txt") or "0") or 0

function draw()
  gfx.font("sans18", "bold")
  local s = tostring(n)
  gfx.text((gfx.width() - gfx.text_width(s)) // 2, gfx.height() // 2, s)
  gfx.hints("Exit", nil, "-", "+")
end

function on_button(b)
  if b == "left" then n = n - 1 elseif b == "right" then n = n + 1 else return end
  fs.write("count.txt", tostring(n))
  sys.redraw()
end

function on_touch(x)
  n = n + (x < gfx.width() // 2 and -1 or 1)
  fs.write("count.txt", tostring(n))
  sys.redraw()
end
