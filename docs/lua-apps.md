# Lua apps

Small on-device programs written in Lua 5.4, loaded from the SD card.

```
/apps/<name>/index.lua    the app's entry script; one directory per app, listed under Plugins → Apps
/apps/<name>/...          the app's other modules and data files
/apps/.lib/<mod>.lua      shared modules
```

An app can read and write inside its own directory (except its own
`index.lua`), can read (but not write) `/apps/.lib`, and can reach other
directories only through the permissions it declares. Relative `fs` paths
resolve inside the app directory. `require("a.b")` loads `/apps/<name>/a/b.lua`, falling back to
`/apps/.lib/a/b.lua`.

The repository's `lua/apps/` mirrors this layout; copy its contents to `/apps`
on the SD card to install the bundled apps and libraries.

The **Apps** row appears in the Plugins picker (Home → Plugins, or Settings →
System → Plugins) whenever an `/apps/<name>/index.lua` exists. Directories
whose names start with `.` are not apps.

## Metadata

Optional comment lines within the first 512 bytes of `index.lua` set the
list row:

```lua
-- title: Calculator
-- description: Four-function calculator
```

Without a title the directory name is shown.

## Permissions

An app declares access outside its directory in `-- permissions:` lines in the
same header:

```lua
-- permissions: read:/.dictionaries read:/dictionaries
-- permissions: write:/notes
```

Entries are separated by spaces or commas; there may be several
`permissions` lines and at most 8 entries in all.

| Entry | Grants |
|---|---|
| `read:<dir>` | `fs` reads (and `dict` lookups) anywhere under the absolute directory `<dir>`. |
| `write:<dir>` | Reads and writes under `<dir>`. May not include `/apps` or lie inside it. |

A malformed entry stops the app from starting, with the reason on the error
screen. Permissions are read once, when the app starts. Because they live in
`index.lua`, an app can never write or remove its own `index.lua`, nor write
anywhere under `/apps` outside its own directory.

Paths are compared the way the SD card resolves them: repeated `/` collapse,
and leading spaces and trailing dots and spaces of each segment are dropped
(`"index.lua."` names `index.lua`). A segment that is empty after trimming
(`"..."`) is rejected.

## Lifecycle

The script's top level runs once at start, then `draw()` runs. After that the
app is event driven; define any of these globals:

| Callback | When |
|---|---|
| `draw()` | The screen needs painting: after start, after `sys.redraw()`, after returning from the keyboard. The framebuffer is cleared to white first. |
| `on_button(name)` | A button was released: `"back"`, `"confirm"`, `"left"`, `"right"`, `"up"`, `"down"`. |
| `on_touch(x, y, kind)` | Touch boards: `kind` is `"tap"` or `"long"`; coordinates are logical screen pixels. |
| `on_swipe(direction)` | Touch boards: a swipe; `direction` is the way the finger moved, `"left"`, `"right"`, `"up"` or `"down"`. A left-edge swipe to the right is Back instead, and system edge gestures (Home, light panel) are not delivered. |
| `on_tick()` | Every `sys.set_tick(ms)` interval. |

Back exits the app unless `on_button("back")` returns `true` (use that to
close an in-app dialog). Holding Back for one second always exits, and the
Home button always goes Home.

Nothing is redrawn automatically after an event: call `sys.redraw()` from a
callback when the screen should change. Redraws are coalesced, so a burst of
input draws once.

An uncaught error shows an error screen with the message; Back closes it.

## `gfx`

Drawing functions are only valid inside `draw()`. `black` defaults to `true`;
pass `false` to draw white.

| Function | Notes |
|---|---|
| `gfx.width()`, `gfx.height()` | Current screen size (orientation aware). |
| `gfx.clear([black])` | Fill the screen (white unless `black` is true). |
| `gfx.pixel(x, y [, black])` | |
| `gfx.line(x1, y1, x2, y2 [, black [, width]])` | |
| `gfx.rect(x, y, w, h [, black])` | |
| `gfx.fill_rect(x, y, w, h [, color])` | `color` is a boolean (true black, false white) or `"black"`, `"white"`, `"light_gray"`, `"dark_gray"`; the greys are dithered. Defaults to black. |
| `gfx.round_rect(x, y, w, h, r [, black])` | |
| `gfx.fill_round_rect(x, y, w, h, r [, color])` | `color` as for `fill_rect`. |
| `gfx.circle(cx, cy, r [, black])`, `gfx.fill_circle(...)` | |
| `gfx.polygon({x1, y1, x2, y2, ...} [, black])` | Closed outline, up to 256 points. |
| `gfx.fill_polygon({...} [, black])` | |
| `gfx.font(name [, style])` | `ui` (default), `ui_large`, `small`, `sans12`–`sans18`, `serif12`–`serif18`; style `regular`, `bold`, `italic`, `bold_italic`. Applies to the text functions below. |
| `gfx.text(x, y, str [, black])` | `y` is the top of the line. |
| `gfx.text_width(str)`, `gfx.line_height()` | Measurement; usable outside `draw()`. |
| `gfx.wrap(str, max_width [, max_lines])` | Word-wraps to a table of lines. |
| `gfx.image(path, x, y [, max_w, max_h])` | Draws a BMP in black and white; returns `true` on success. |
| `gfx.hints(back, confirm, left, right)` | The theme's button-hint bar along the bottom edge; `nil` hides a label. Returns the bar's height. |

## `sys`

| Function | Notes |
|---|---|
| `sys.redraw([mode])` | Schedule `draw()`. `mode`: `"fast"` (default), `"half"`, `"full"` (slowest; clears ghosting). |
| `sys.exit()` | Close the app. |
| `sys.millis()` | Milliseconds since boot. |
| `sys.set_tick(ms)` | Call `on_tick()` every `ms` (minimum 250); `0` stops. |
| `sys.keyboard(title, initial, callback [, max_len])` | Opens the on-screen keyboard; `callback(text)` on confirm, `callback(nil)` on cancel. |
| `sys.heap()` | Bytes used by this app's Lua heap, and the cap. |
| `sys.has_touch()` | Whether the board has a touchscreen. |
| `sys.app_dir()` | Absolute path of the app's directory, e.g. `/apps/notepad`. |

`os.time`, `os.date`, `os.clock` and `os.difftime` are available (the clock is
only correct once it has been synced). `print` writes to the serial log.

## `fs`

Relative paths resolve inside the app's directory. Absolute paths must lie
inside it, inside `/apps/.lib` for reading, or inside a directory granted by
the app's [permissions](#permissions); anything else, and any write to the
app's own `index.lua`, raises an "access denied" error. `.` and `..` path segments are rejected. Create
subdirectories with `fs.mkdir` before writing into them.

| Function | Returns |
|---|---|
| `fs.read(path)` | Whole file (up to 32KB), or `nil, err`. |
| `fs.read(path, offset [, length])` | Up to `length` bytes (at most 32KB) from `offset`; `""` past the end. For larger files, read in chunks (see `lua/apps/.lib/lines.lua`). |
| `fs.size(path)` | File size in bytes, or `nil, err`. |
| `fs.inflate(path, offset, clen, skip, len)` | Decompresses the raw deflate stream stored at bytes `[offset, offset+clen)` of the file and returns its output bytes `[skip, skip+len)` (`len` at most 16KB; shorter if the stream ends first), or `nil, err`. Holds about 42KB outside the Lua heap while it runs; `err` is `"not enough memory"`, `"cannot open"` or `"decompress failed"`. |
| `fs.write(path, data)` | `true`, or `nil, err`. Replaces the file. |
| `fs.write(path, data, offset)` | Overwrites bytes in place from `offset` (at most the current size), extending the file if needed. Use it for fixed-width records kept on the card instead of in memory. |
| `fs.append(path, data)` | `true`, or `nil, err`. |
| `fs.exists(path)`, `fs.remove(path)`, `fs.mkdir(path)` | Boolean. |
| `fs.list(path)` | Table of names (directories end in `/`), or `nil, err`. |

## `dict`

StarDict lookups, using the same engine as the reader's dictionary (see
[dictionary.md](dictionary.md)). `folder` is a dictionary folder (holding
`<stem>.idx` and `<stem>.dict` or `.dict.dz`) the app may read, typically
granted with `read:/.dictionaries`.

| Function | Returns |
|---|---|
| `dict.status(folder)` | `"ready"`, `"needs_index"`, or `nil, "not a dictionary"`. |
| `dict.lookup(folder, word)` | `definition, headword` or `nil, err`. |

`dict.lookup` cleans the word and falls back to synonyms and English stems as
the reader does. The definition is plain text (HTML is converted, paragraphs
separated by `\n`), cut to 16KB. `err` is `"not found"`, `"not a
dictionary"`, `"not enough memory"`, `"read failed"`, `"decompress failed"` or
`"index failed"`.

When `dict.status` reports `"needs_index"`, the next lookup first writes the
`.qidx`/`.sidx` sidecar files next to the dictionary, as the reader does; that
can take several seconds, during which the app does not respond. Show a
message and do the lookup from `on_tick` so it is drawn first (see
`lua/apps/dictionary`).

## Limits

- Lua heap is capped at 96KB per app (144KB in the 64-bit simulator, where
  Lua objects are larger), and allocations fail early if the device would
  drop below 40KB free; running out raises a "not enough memory" error.
  Compiled code counts against the cap, and is several times the size of its
  source.
- Numbers are 32-bit (`LUA_32BITS`): integers wrap at ±2^31 and floats are
  single precision.
- Scripts and modules are text only (no precompiled bytecode) and at most
  32KB each. `load()` takes a source string only, not a reader function.
- C-level nesting (deeply nested expressions, C-to-Lua callbacks such as
  `table.sort` comparators) is limited to 32 levels; ordinary Lua recursion is
  not affected.
- Not available: `io`, `package`, `debug`, `dofile`, `loadfile`, and
  `os.execute`/`exit`/`getenv`/`remove`/`rename`/`tmpname`/`setlocale`.

## Example

```lua
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
  sys.redraw()
end
```
