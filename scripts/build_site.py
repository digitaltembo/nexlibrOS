#!/usr/bin/env python3
"""Build the nexlibrOS website (site/src) into a static folder, ready for any static host.

    python3 scripts/build_site.py [--out build/site] [--build]
                                  [--compare "CrossPoint Reader=PATH"]... [--sd IMAGE] [--no-demo]

Pages: home, about (README blurb + USER_GUIDE.md), download (the release build), apps (one zip
per lua/apps app, plus all of them), and demo, which boots the firmware in esp32sim's WebAssembly
build (the esp32sim submodule's web page is copied to sim/ as the developer view).

Inputs, all from this checkout:
  - the firmware from `pio run -e x4pro-gh_release` (--env picks another environment);
  - esp32sim/web/wasm/esp32sim.wasm from esp32sim/tools/wasm-build.sh, and the ESP32-S3 mask ROM
    esp32sim/web/wasm/fw/esp32s3_rev0_rom.elf from esp32sim/tools/fetch-rom-elfs.sh web/wasm/fw;
  - the demo SD card: lua/apps and the user guide EPUBs, with site/src/card merged over them (Walden,
    and a sample Wikipedia pack from site/wiki-sample-titles.txt, see its data/ABOUT.txt), written as
    a FAT16 image by scripts/fat_image.py. --sd replaces it with any raw MBR + FAT image; the card
    is published, so it must hold nothing you may not redistribute.
--build runs the firmware and wasm builds first. --compare adds another firmware to the demo:
PATH is a PlatformIO build directory (firmware.bin, and bootloader.bin and partitions.bin when
they differ from ours) or a lone app image such as a release's .bin. Standard library only.
"""

import argparse
import gzip
import hashlib
import html
import json
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

import fat_image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "site" / "src"
SIM = ROOT / "esp32sim"
REPO = "digitaltembo/nexlibrOS"
BOOT_APP0 = Path.home() / ".platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
# the files whose URLs carry a content hash, so a cached old copy is never paired with a new page
STAMPED = ("style.css", "site.js", "demo.js", "sim/touch-input.js", "sim/wasm/worker.js", "sim/wasm/esp32sim.wasm")
ROM = "esp32s3_rev0_rom.elf"
# what the developer view (esp32sim's run.html) needs for an S3 board
SIM_FILES = ["run.html", "emu.js", "terminal.js", "touch-input.js", "workbench.js", "workbench.css",
             "assets/esp32sim-logo-icon.png", "vendor/xterm/xterm.js", "vendor/xterm/xterm.css", "vendor/xterm/LICENSE",
             "wasm/esp32sim.wasm", "wasm/worker.js", "wasm/jit.mjs", "wasm/pacing.mjs", "wasm/experiments.mjs", "LICENSE"]


def fail(msg):
    sys.exit(f"build_site: {msg}")


def size_text(n):
    return f"{n / 1048576:.1f} MB" if n >= 1048576 else f"{max(1, round(n / 1024))} KB"


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "firmware"


# ---- Markdown: the subset USER_GUIDE.md uses (GitHub flavour: tables, fences, alerts, raw <img>)

class Markdown:
    def __init__(self, link):
        self.link = link      # rewrites a link target (relative repository paths -> GitHub)
        self.ids = {}

    def anchor(self, text):
        # GitHub's heading ids: lower case, punctuation dropped, each space a hyphen
        base = re.sub(r"[^\w\- ]", "", text.lower()).replace(" ", "-")
        n = self.ids.get(base, 0)
        self.ids[base] = n + 1
        return base if n == 0 else f"{base}-{n}"

    def inline(self, s):
        keep = []

        def stash(h):
            keep.append(h)
            return f"\x00{len(keep) - 1}\x00"

        s = re.sub(r"(`+)(.+?)\1", lambda m: stash(f"<code>{html.escape(m.group(2).strip())}</code>"), s)
        s = re.sub(r"<img\b[^>]*>", lambda m: stash(m.group(0)), s)
        s = re.sub(r"\\([\\`*_{}\[\]()#+\-.!&|<>])", lambda m: stash(html.escape(m.group(1))), s)
        s = html.escape(s, quote=False)
        s = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", lambda m: stash(f'<img alt="{html.escape(m.group(1))}" src="{html.escape(self.link(m.group(2)))}" loading="lazy">'), s)
        s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: f'<a href="{html.escape(self.link(html.unescape(m.group(2))))}">{m.group(1)}</a>', s)
        s = re.sub(r"\*\*(.+?)\*\*", r"<b>\1</b>", s)
        s = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", s)
        s = re.sub(r"(?<![\w])_(?!\s)(.+?)(?<!\s)_(?![\w])", r"<em>\1</em>", s)
        return re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)

    LIST = re.compile(r"^( *)([-*+]|\d+[.)]) +(.*)$")

    def blocks(self, lines):
        out, i = [], 0
        while i < len(lines):
            line = lines[i]
            if not line.strip():
                i += 1
                continue
            if line.lstrip().startswith("```"):
                indent = len(line) - len(line.lstrip())
                body, i = [], i + 1
                while i < len(lines) and not lines[i].lstrip().startswith("```"):
                    body.append(lines[i][indent:] if lines[i][:indent].strip() == "" else lines[i])
                    i += 1
                out.append(f"<pre><code>{html.escape(chr(10).join(body))}</code></pre>")
                i += 1
                continue
            m = re.match(r"^(#{1,6}) +(.*?) *#*$", line)
            if m:
                level, text = len(m.group(1)), m.group(2)
                plain = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text).replace("\\", "")
                out.append(f'<h{level} id="{self.anchor(plain)}">{self.inline(text)}</h{level}>')
                i += 1
                continue
            if re.match(r"^ {0,3}([-*_])( *\1){2,} *$", line):
                out.append("<hr>")
                i += 1
                continue
            if line.lstrip().startswith("|") and i + 1 < len(lines) and re.match(r"^ *\|?[ :\-|]+\|[ :\-|]*$", lines[i + 1]):
                rows = []
                while i < len(lines) and lines[i].lstrip().startswith("|"):
                    rows.append(lines[i])
                    i += 1
                cells = lambda r: [c.strip() for c in re.split(r"(?<!\\)\|", r.strip().strip("|"))]
                head = "".join(f"<th>{self.inline(c)}</th>" for c in cells(rows[0]))
                body = "".join("<tr>" + "".join(f"<td>{self.inline(c)}</td>" for c in cells(r)) + "</tr>" for r in rows[2:])
                out.append(f"<table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table>")
                continue
            if line.startswith(">"):
                quote = []
                while i < len(lines) and lines[i].startswith(">"):
                    quote.append(re.sub(r"^> ?", "", lines[i]))
                    i += 1
                alert = re.match(r"^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$", quote[0]) if quote else None
                if alert:
                    inner = f'<p class="callout-title">{alert.group(1).title()}</p>' + self.blocks(quote[1:])
                    out.append(f'<blockquote class="callout">{inner}</blockquote>')
                else:
                    out.append(f"<blockquote>{self.blocks(quote)}</blockquote>")
                continue
            if self.LIST.match(line):
                html_list, i = self.list(lines, i)
                out.append(html_list)
                continue
            if re.match(r"^<(img|div|p|br|table|details|picture)\b", line.strip()):
                out.append(line.strip())
                i += 1
                continue
            para = []
            while i < len(lines) and lines[i].strip() and not self.starts_block(lines[i]):
                para.append(lines[i].strip())
                i += 1
            out.append(f"<p>{self.inline(' '.join(para))}</p>")
        return "\n".join(out)

    def starts_block(self, line):
        return bool(re.match(r"^(#{1,6} |```| *\||>|( *)([-*+]|\d+[.)]) )", line)) or bool(re.match(r"^ {0,3}([-*_])( *\1){2,} *$", line))

    def list(self, lines, i):
        first = self.LIST.match(lines[i])
        indent, ordered = len(first.group(1)), first.group(2)[0].isdigit()
        items = []
        while i < len(lines):
            m = self.LIST.match(lines[i])
            if not m or len(m.group(1)) != indent or m.group(2)[0].isdigit() != ordered:
                break
            content_indent = len(m.group(1)) + len(m.group(2)) + 1
            body, i = [m.group(3)], i + 1
            # the item's own lines: indented under it, or a lazy continuation, or blank lines inside it
            while i < len(lines):
                l = lines[i]
                if not l.strip():
                    nxt = next((x for x in lines[i + 1:] if x.strip()), "")
                    if len(nxt) - len(nxt.lstrip()) > indent:
                        body.append("")
                        i += 1
                        continue
                    break
                lead = len(l) - len(l.lstrip())
                if lead > indent:
                    body.append(l[min(lead, content_indent):] if lead >= content_indent else l.lstrip())
                elif not self.starts_block(l) and body[-1].strip():
                    body.append(l.strip())
                else:
                    break
                i += 1
            items.append(body)
            # a blank line between items of the same list
            if i < len(lines) and not lines[i].strip():
                j = i
                while j < len(lines) and not lines[j].strip():
                    j += 1
                n = self.LIST.match(lines[j]) if j < len(lines) else None
                if n and len(n.group(1)) == indent and n.group(2)[0].isdigit() == ordered:
                    i = j
        start = ""
        if ordered and first.group(2)[:-1] != "1":
            start = f' start="{first.group(2)[:-1]}"'
        tag = "ol" if ordered else "ul"
        html_items = []
        for body in items:
            inner = self.blocks(body)
            # a tight item: its single paragraph needs no <p>
            inner = re.sub(r"^<p>(.*?)</p>(?=\n<(ul|ol)>|$)", r"\1", inner, count=1, flags=re.S)
            html_items.append(f"<li>{inner}</li>")
        return f"<{tag}{start}>{''.join(html_items)}</{tag}>", i

    def render(self, text):
        return self.blocks(text.replace("\t", "    ").splitlines())


# ---- pages

def fragment(name):
    text = (SRC / "pages" / name).read_text()
    meta = dict(re.findall(r"<!-- (\w+): (.*?) -->\n", text))
    return meta, re.sub(r"<!-- \w+: .*? -->\n", "", text)


def fill(text, values):
    def sub(m):
        if m.group(1) not in values:
            fail(f"no value for {{{{{m.group(1)}}}}}")
        return str(values[m.group(1)])
    return re.sub(r"\{\{(\w+)\}\}", sub, text)


STATUSBAR = ('<div class="statusbar"><span class="clock"></span><span class="battery">100%<svg viewBox="0 0 22 12">'
             '<rect x="1" y="1" width="17" height="10" rx="1.5"/><rect x="19" y="4" width="2" height="4"/>'
             '<rect x="3" y="3" width="13" height="6" class="fill"/></svg></span></div>')
BACK = '<svg viewBox="0 0 24 24"><path d="M20 12H5M11 5.5 4.5 12l6.5 6.5"/></svg>'


def page(out, name, file, values, body_class="", outside="", scripts=""):
    meta, inner = fragment(file)
    inner = fill(inner, values)
    if meta.get("layout") == "demo":
        screen = inner
    elif "heading" in meta:
        screen = (f'<div class="page">{STATUSBAR}<header class="titlebar"><a class="back" href="index.html" aria-label="Back to the home page">{BACK}</a>'
                  f'<h1>{meta["heading"]}</h1></header><div class="content" tabindex="-1">{inner}</div>'
                  f'<footer class="pagebar"><span>{meta["heading"]}</span><span class="progress"></span></footer></div>')
    else:
        screen = f'<div class="page">{STATUSBAR}{inner}</div>'
    doc = fill((SRC / "page.html").read_text(), {
        "title": html.escape(fill(meta["title"], values)), "description": html.escape(fill(meta["description"], values)),
        "body_class": body_class, "outside": outside, "screen": screen, "scripts": scripts})
    (out / name).write_text(doc)


def lua_header(path):
    head = path.read_bytes()[:512].decode("utf-8", "replace")
    return {k: v.strip() for k, v in re.findall(r"^--\s*(title|description):\s*(.*)$", head, re.M)}


def guide_apps(md):
    """The guide's app table: title -> (what it does, where its files go)."""
    rows = {}
    for m in re.finditer(r"^\| \*\*(.+?)\*\* \| (.+?) \| (.+?) \|$", md, re.M):
        rows[m.group(1)] = (m.group(2), m.group(3))
    return rows


def zip_tree(zf, src, arc):
    for p in sorted(src.rglob("*")):
        if p.is_file() and p.name != ".DS_Store":
            zf.write(p, f"{arc}/{p.relative_to(src).as_posix()}")


def build_apps(out, md, inline):
    apps_dir = ROOT / "lua" / "apps"
    lib = apps_dir / ".lib"
    dest = out / "apps"
    dest.mkdir()
    rows, described = [], guide_apps(md)
    for app in sorted(p for p in apps_dir.iterdir() if p.is_dir() and not p.name.startswith(".") and (p / "index.lua").exists()):
        meta = lua_header(app / "index.lua")
        title = meta.get("title", app.name)
        # shared modules the app requires that it does not carry itself
        needs = sorted({m for f in app.rglob("*.lua") for m in re.findall(r"require\(\s*[\"']([\w.]+)[\"']\s*\)", f.read_text())
                        if not (app / (m.replace(".", "/") + ".lua")).exists() and (lib / (m.replace(".", "/") + ".lua")).exists()})
        zpath = dest / f"{app.name}.zip"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
            zip_tree(zf, app, f"apps/{app.name}")
            for m in needs:
                zf.write(lib / (m.replace(".", "/") + ".lua"), f"apps/.lib/{m.replace('.', '/')}.lua")
        does, where = described.get(title, (meta.get("description", ""), "—"))
        where_html = "" if where.strip() in ("—", "-", "") else f'<p class="where">Files: {inline(where)}</p>'
        rows.append(f'<li><h2>{html.escape(title)}</h2><p>{inline(does)}</p>{where_html}'
                    f'<a class="get" href="apps/{zpath.name}" download>Download<small>{size_text(zpath.stat().st_size)} zip</small></a></li>')
    everything = dest / "nexlibrOS-apps.zip"
    with zipfile.ZipFile(everything, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zip_tree(zf, apps_dir, "apps")
    return rows, everything


# ---- firmware, the demo card and the emulator

def build_firmware(env):
    run(["pio", "run", "-e", env], ROOT)


def run(cmd, cwd):
    exe = shutil.which(cmd[0]) or (str(Path.home() / ".platformio/penv/bin/pio") if cmd[0] == "pio" else None)
    if not exe or not Path(exe).exists():
        fail(f"{cmd[0]} not found")
    print("+", " ".join(cmd), flush=True)
    subprocess.run([exe, *cmd[1:]], cwd=cwd, check=True)


def make_card(dest):
    """A FAT16 card with the apps, the user guide, and site/src/card merged over them (books, and
    app data such as the sample Wikipedia pack): mostly empty, so it gzips to little."""
    with tempfile.TemporaryDirectory() as tmp:
        stage = Path(tmp) / "card"
        shutil.copytree(ROOT / "lua" / "apps", stage / "apps", ignore=shutil.ignore_patterns(".DS_Store"))
        for book in (ROOT / "nexlibrOS_User_Guide.epub", ROOT / "nexlibrOS_Documentation.epub"):
            if book.is_file():
                shutil.copy2(book, stage / book.name)
        if (SRC / "card").is_dir():
            shutil.copytree(SRC / "card", stage, dirs_exist_ok=True, ignore=shutil.ignore_patterns(*fat_image.SKIP))
        img = Path(tmp) / "card.img"
        fat_image.write(img, stage)
        with open(img, "rb") as raw, gzip.GzipFile(dest, "wb", 9, mtime=0) as gz:
            shutil.copyfileobj(raw, gz, 1 << 20)


def firmware_set(path, ours):
    """(bootloader, partitions, app) for a --compare PATH, falling back to our bootloader and table."""
    path = Path(path).expanduser()
    if path.is_file():
        return ours[0], ours[1], path
    if not (path / "firmware.bin").exists():
        fail(f"{path}: no firmware.bin")
    pick = lambda name, fallback: path / name if (path / name).exists() else fallback
    return pick("bootloader.bin", ours[0]), pick("partitions.bin", ours[1]), path / "firmware.bin"


def stamp(out):
    """Add ?v=<content hash> to the references to STAMPED files in the pages and demo.js."""
    tags = {}
    # demo.js names the worker and the module, so it is rewritten before its own hash is taken
    for page_file in [out / "demo.js", *out.glob("*.html")]:
        if not page_file.exists():
            continue
        text = page_file.read_text()
        for f in STAMPED:
            if (out / f).exists() and f != page_file.name:
                tags.setdefault(f, hashlib.sha256((out / f).read_bytes()).hexdigest()[:10])
                text = re.sub(rf"(['\"]){re.escape(f)}(['\"])", rf"\g<1>{f}?v={tags[f]}\g<2>", text)
        page_file.write_text(text)


def build_demo(out, version, ours, app_href, compares, sd, boot_app0):
    web = SIM / "web"
    for need, hint in ((web / "wasm/esp32sim.wasm", "build it: (cd esp32sim && tools/wasm-build.sh), or run with --build"),
                       (web / "wasm/fw" / ROM, "fetch it: (cd esp32sim && tools/fetch-rom-elfs.sh web/wasm/fw)"),
                       (web / "vendor/xterm/xterm.js", "fetch it: esp32sim/tools/fetch-web-vendor.sh")):
        if not need.exists():
            fail(f"{need.relative_to(ROOT)} is missing; {hint}")
    if not boot_app0.exists():
        fail(f"{boot_app0} is missing (it comes with PlatformIO's Arduino framework; --boot-app0 names another copy)")
    sim = out / "sim"
    for f in SIM_FILES:
        (sim / f).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(SIM / f if f == "LICENSE" else web / f, sim / f)
    fw = sim / "wasm" / "fw"
    fw.mkdir(parents=True, exist_ok=True)
    shutil.copy2(web / "wasm/fw" / ROM, fw / ROM)
    shutil.copy2(boot_app0, fw / "boot_app0.bin")
    card = fw / "sd.img.gz"
    if sd:
        data = Path(sd).expanduser().read_bytes()
        card.write_bytes(data if data[:2] == b"\x1f\x8b" else gzip.compress(data, 9, mtime=0))
    else:
        make_card(card)

    entries = [("nexlibros", "nexlibrOS", f"{version}, this site's build", ours[0], ours[1], app_href)]
    for spec in compares:
        if "=" not in spec:
            fail(f"--compare wants NAME=PATH, got {spec!r}")
        name, path = spec.split("=", 1)
        boot, table, app = firmware_set(path, ours)
        entries.append((slug(name), name.strip(), "for comparison", boot, table, app))
    demos = []
    for fid, title, note, boot, table, app in entries:
        d = fw / fid
        d.mkdir(exist_ok=True)
        files = {"rom": ROM, "bootloader": f"{fid}/bootloader.bin", "ptable": f"{fid}/partitions.bin", "sd": "sd.img.gz"}
        shutil.copy2(boot, d / "bootloader.bin")
        shutil.copy2(table, d / "partitions.bin")
        if isinstance(app, str):
            files["app"] = app          # ours: the download itself, not a second copy
        else:
            shutil.copy2(app, d / "firmware.bin")
            files["app"] = f"{fid}/firmware.bin"
        manifest = {"board": "xteink-x4pro", "flash_mb": 16, "psram_mb": 8, "files": files, "flash_at": {"0xe000": "boot_app0.bin"}}
        (fw / f"{fid}.json").write_text(json.dumps(manifest, indent=1) + "\n")
        sizes = {u: (fw / u).resolve().stat().st_size for u in [*files.values(), "boot_app0.bin"]}
        demos.append({"fw": fid, "title": title, "note": note, "bytes": sizes, "wasm_bytes": (sim / "wasm/esp32sim.wasm").stat().st_size})
    # demos.json is also the developer view's list of demos
    (fw / "demos.json").write_text(json.dumps(demos, indent=1) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=str(ROOT / "build" / "site"), help="output folder (replaced)")
    ap.add_argument("--env", default="x4pro-gh_release", help="PlatformIO environment of the published firmware")
    ap.add_argument("--build", action="store_true", help="build the firmware and the emulator's wasm first")
    ap.add_argument("--compare", action="append", default=[], metavar="NAME=PATH", help="another firmware for the demo")
    ap.add_argument("--sd", help="demo SD card image (raw, or gzipped) instead of building one")
    ap.add_argument("--no-demo", action="store_true", help="leave out the demo and the emulator")
    ap.add_argument("--branch", default="main", help="branch for links into the repository")
    ap.add_argument("--boot-app0", default=str(BOOT_APP0), help="the OTA data image flashed at 0xe000 (default: PlatformIO's)")
    args = ap.parse_args()

    if args.build:
        build_firmware(args.env)
        if not args.no_demo:
            run(["sh", "tools/wasm-build.sh"], SIM)
    build = ROOT / ".pio" / "build" / args.env
    ours = (build / "bootloader.bin", build / "partitions.bin", build / "firmware.bin")
    for f in ours:
        if not f.exists():
            fail(f"{f.relative_to(ROOT)} is missing; build it with `pio run -e {args.env}`, or run with --build")

    version = re.search(r"^version = (\S+)", (ROOT / "platformio.ini").read_text(), re.M).group(1)
    repo_url, repo_blob = f"https://github.com/{REPO}", f"https://github.com/{REPO}/blob/{args.branch}"
    out = Path(args.out).resolve()
    if out == ROOT or ROOT.is_relative_to(out):
        fail(f"refusing to replace {out}")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    def link(target):
        if re.match(r"^[a-z]+:|^#|^/", target):
            return target
        return f"{repo_blob}/{target[2:] if target.startswith('./') else target}"

    md_text = (ROOT / "USER_GUIDE.md").read_text()
    md = Markdown(link)
    guide = md.render(md_text)
    inline = Markdown(link).inline

    (out / "firmware").mkdir()
    fw_name = f"nexlibrOS-{version}-x4pro.bin"
    shutil.copy2(ours[2], out / "firmware" / fw_name)
    app_rows, all_apps = build_apps(out, md_text, inline)

    values = {
        "version": version, "repo": REPO, "repo_url": repo_url, "repo_blob": repo_blob, "guide": guide,
        "app_count": len(app_rows), "apps": "\n".join(app_rows),
        "all_apps_href": f"apps/{all_apps.name}", "all_apps_size": size_text(all_apps.stat().st_size),
        "firmware_name": fw_name, "firmware_href": f"firmware/{fw_name}", "firmware_size": size_text(ours[2].stat().st_size),
        "firmware_sha256": hashlib.sha256(ours[2].read_bytes()).hexdigest(),
    }
    for f in ("style.css", "site.js", "background.jpg", "icon.svg", "logo.svg"):
        shutil.copy2(SRC / f, out / f)
    page(out, "index.html", "home.html", values, "home-page")
    if args.no_demo:
        home = out / "index.html"
        home.write_text(re.sub(r'\s*<a href="demo\.html">.*?</a>', "", home.read_text()))
    page(out, "about.html", "about.html", values)
    page(out, "download.html", "download.html", values)
    page(out, "apps.html", "apps.html", values)
    if not args.no_demo:
        build_demo(out, version, ours, f"../../../firmware/{fw_name}", args.compare, args.sd, Path(args.boot_app0))
        shutil.copy2(SRC / "demo.js", out / "demo.js")
        page(out, "demo.html", "demo.html", values, "demo", fragment("demo-panel.html")[1],
             '<script src="sim/touch-input.js"></script>\n<script src="demo.js"></script>')
    stamp(out)
    (out / ".nojekyll").write_text("")   # GitHub Pages: serve the folder as is
    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"site: {out} ({size_text(total)})")
    print(f"preview: python3 -m http.server -d {out} 8800  ->  http://127.0.0.1:8800/")


if __name__ == "__main__":
    main()
