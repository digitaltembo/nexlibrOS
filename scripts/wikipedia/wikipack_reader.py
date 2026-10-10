#!/usr/bin/env python3
"""Reference reader for packs written by build_wiki_pack.py.

Mirrors what the Lua app does on the device: bounded fs.read(path, offset, len)
calls and a native inflate(path, offset, clen, skip, len) that streams one raw
deflate block through a 32KB window and returns only the requested slice.

  wikipack_reader.py PACK search <prefix>
  wikipack_reader.py PACK show <title-or-#id> [max_bytes]
  wikipack_reader.py PACK random
  wikipack_reader.py PACK verify [N]       # every key resolves, every article decodes (or N samples)
"""

import random
import re
import struct
import sys
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from build_wiki_pack import norm_key  # noqa: E402

MAX_READ = 32 * 1024
LINK = re.compile(b"\x06([^\x07]*)\x07(...)", re.S)


def link_ids(text):
    return [(a - 48) << 12 | (b - 48) << 6 | (c - 48) for _, (a, b, c) in LINK.findall(text)]
SLICE = 8 * 1024


class Pack:
    def __init__(self, root):
        self.root = root
        self.reads = 0
        self.hdr = {}
        for line in open(f"{root}/wiki.hdr"):
            k, v = line.rstrip("\n").split("=", 1)
            self.hdr[k] = int(v) if v.isdigit() else v

    def read(self, name, offset, length):
        assert length <= MAX_READ
        self.reads += 1
        with open(f"{self.root}/{name}", "rb") as f:
            f.seek(offset)
            return f.read(length)

    def inflate(self, name, offset, clen, skip, length):
        d = zlib.decompressobj(-15)
        out, pos = bytearray(), 0
        while pos < clen and len(out) < skip + length:
            chunk = self.read(name, offset + pos, min(4096, clen - pos))
            pos += len(chunk)
            out += d.decompress(chunk)
            if len(out) > skip + length:
                break
        return bytes(out[skip:skip + length])

    # --- articles ------------------------------------------------------------

    def record(self, aid):
        return struct.unpack("<IIII", self.read("articles.bin", aid * 16, 16))

    def title(self, aid):
        toff = self.record(aid)[3]
        n = self.read("titles.bin", toff, 1)[0]
        return self.read("titles.bin", toff + 1, n).decode()

    def text(self, aid, skip=0, length=SLICE):
        """Bytes [skip, skip+length) of the article, crossing block boundaries."""
        block, inner, size, _ = self.record(aid)
        length = min(length, size - skip)
        braw = self.hdr["block_raw"]
        block, inner = block + (inner + skip) // braw, (inner + skip) % braw
        out = bytearray()
        while len(out) < length:
            lo, hi = struct.unpack("<II", self.read("blocks.bin", block * 4, 8))
            out += self.inflate("text.bin", lo, hi - lo, inner, min(length - len(out), braw - inner))
            block, inner = block + 1, 0
        return bytes(out)

    # --- search --------------------------------------------------------------

    def _page_first(self, i):
        return self.read("pages.bin", i * self.hdr["key_prefix"], self.hdr["key_prefix"]).rstrip(b"\0")

    def _page_entries(self, i):
        page = self.read("index.bin", i * self.hdr["page_bytes"], self.hdr["page_bytes"])
        pos, key = 0, b""
        while pos + 2 <= len(page) and page[pos + 1]:
            shared, n = page[pos], page[pos + 1]
            key = key[:shared] + page[pos + 2:pos + 2 + n]
            pos += 2 + n
            v, shift = 0, 0
            while True:
                b = page[pos]
                pos += 1
                v |= (b & 0x7F) << shift
                shift += 7
                if not b & 0x80:
                    break
            yield key, v >> 1, v & 1

    def search(self, query, limit=10, scan_pages=3):
        q = norm_key(query)
        qp = q[:self.hdr["key_prefix"]]
        lo, hi = 0, self.hdr["pages"] - 1
        while lo < hi:  # last page whose first key < qp (or page 0)
            mid = (lo + hi + 1) // 2
            if self._page_first(mid) < qp:
                lo = mid
            else:
                hi = mid - 1
        hits = {}
        for p in range(lo, min(lo + scan_pages, self.hdr["pages"])):
            done = False
            for key, aid, redirect in self._page_entries(p):
                if key.startswith(q):
                    exact = key == q
                    best = hits.get(aid)
                    if best is None or (exact and not best[0]):
                        hits[aid] = (exact, key, redirect)
                elif key > q:
                    done = True
                    break
            if done:
                break
        # Exact matches first, then popularity (ids are in rank order).
        ranked = sorted(hits.items(), key=lambda kv: (not kv[1][0], kv[0]))[:limit]
        return [(aid, self.title(aid), info[1].decode(), info[2]) for aid, info in ranked]


def render(text):
    text = LINK.sub(rb"[\1]", text)
    for line in text.decode("utf-8", "replace").split("\n"):
        if line.startswith("\x01"):
            print("\n" + "#" * int(line[1]) + " " + line[2:])
        elif line.startswith("\x02"):
            print("  • " + line[1:])
        elif line.startswith("\x03"):
            k, _, v = line[1:].partition("\x1f")
            print(f"  [{k}] {v}")
        elif line == "\x05":
            print("\n[article truncated]")
        else:
            print(line + "\n")


def main():
    pack = Pack(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == "search":
        for aid, title, key, redirect in pack.search(" ".join(sys.argv[3:])):
            print(f"#{aid:<6} {title}" + (f"   (via '{key}')" if redirect else ""))
        print(f"[{pack.reads} reads]")
    elif cmd in ("show", "random"):
        if cmd == "random":
            aid = random.randrange(pack.hdr["articles"])
            while not pack.record(aid)[2]:
                aid = random.randrange(pack.hdr["articles"])
        elif sys.argv[3].startswith("#"):
            aid = int(sys.argv[3][1:])
        else:
            aid = pack.search(sys.argv[3], 1)[0][0]
        n = int(sys.argv[4]) if len(sys.argv) > 4 else 3000
        print(f"=== #{aid} {pack.title(aid)} ({pack.record(aid)[2]} bytes) ===")
        render(pack.text(aid, 0, n))
        print(f"[{pack.reads} reads]")
    elif cmd == "verify":
        n = pack.hdr["articles"]
        step = max(1, n // int(sys.argv[3])) if len(sys.argv) > 3 else 1  # verify PACK [samples]
        for aid in range(0, n, step):
            size = pack.record(aid)[2]
            if not size:
                continue
            body = pack.text(aid, 0, size)
            assert len(body) == size, aid
            body.decode("utf-8")
            assert all(t < n for t in link_ids(body)), aid
            # Slices joined at arbitrary boundaries reproduce the whole text.
            mid = size // 3
            assert pack.text(aid, 0, mid) + pack.text(aid, mid, size - mid) == body, aid
        bad = 0
        for aid in range(0, n, step):  # every article is found by its own title
            if pack.record(aid)[2] and aid not in [h[0] for h in pack.search(pack.title(aid), 50, 50)]:
                bad += 1
        for p in range(0, pack.hdr["pages"], step if step == 1 else max(1, pack.hdr["pages"] // 200)):
            for key, aid, _ in pack._page_entries(p):
                hits = [h[0] for h in pack.search(key.decode(), limit=50, scan_pages=50)]
                bad += aid not in hits
        print(f"verified {n} articles, {pack.hdr['keys']} keys, {bad} unresolved keys or titles")
        sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
