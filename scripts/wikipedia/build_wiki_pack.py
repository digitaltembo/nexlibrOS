#!/usr/bin/env python3
"""Build a compact offline Wikipedia pack for the CrossPoint Lua Wikipedia app.

Pipeline: ranking (most popular first) -> HTML source -> plain-text extraction
-> selection under a byte budget -> pack files (format: see FORMAT below).

Sources of rendered (Parsoid) HTML:
  --rest-cache DIR   fetch from the Wikipedia REST API, cached in DIR. For small
                     samples only (one request per article).
  --zim FILE         a Kiwix ZIM, e.g. wikipedia_en_top_nopic_YYYY-MM.zim.
                     Needs `pip install libzim`. With --all, every article is
                     packed untruncated in one parallel pass (no budget).

Ranking: --ranking FILE, one title per line, optionally "title<TAB>weight"
(WP1 tops/*.tsv, scores.tsv or pageviews.tsv from download.openzim.org/wp1/enwiki/).
Without weights, weight = 1/(rank+10).

Selection policies (--policy):
  full       whole articles in rank order until the budget is spent
             (--max-article-bytes caps a single article).
  waterfill  every byte of article a at text offset t is worth
             w_a / (1 + t/T0); keep the most valuable bytes that fit. Popular
             articles get more sections, the long tail gets its lead.

FORMAT (all integers little-endian; "stream" = concatenated article texts):
  wiki.hdr      text, key=value lines (version, counts, block_raw, page_bytes)
  text.bin      raw-deflate blocks; block i holds stream[i*block_raw, (i+1)*block_raw)
  blocks.bin    u32 file offset of each block in text.bin, plus a final end offset
  articles.bin  16 bytes per article id (ids in rank order):
                u32 block, u32 offset inside the block's output, u32 text length
                (0: no text; such ids have no search keys), u32 offset in titles.bin
  titles.bin    article titles, u8 length + UTF-8 bytes
  index.bin     search keys sorted, in page_bytes pages; entries never span pages.
                entry: u8 shared prefix len, u8 suffix len, suffix, varint(id<<1|is_redirect).
                A page's first entry has shared=0; an entry with suffix len 0 ends the page.
  pages.bin     first key of each index page, truncated/zero-padded to 32 bytes

Article text: UTF-8 lines separated by "\\n".
  "\\x01" + level digit + text   heading (2..4)
  "\\x02" + text                 list item
  "\\x03" + key + "\\x1f" + value  infobox row
  "\\x05"                        article was truncated here
  anything else                 paragraph
  Inside any line, "\\x06" + text + "\\x07" + 3 id bytes is a link to that article id:
  each byte holds 6 bits (byte - 0x30), most significant first. Only the first link
  to a target in each section is kept, and only links to articles in the pack.
"""

import argparse
import html as htmllib
import json
import os
import re
import struct
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
import zlib
from array import array

FORMAT_VERSION = 3
KEY_PREFIX_BYTES = 32
MAX_KEY_BYTES = 255
MIN_ARTICLE_BYTES = 200
STUB_MAX_BYTES = 1024
STUB_LINK = re.compile(r'<body>\s*<a href="\./([^"#]+)(?:#[^"]*)?">[^<]*</a>\s*</body>')

DROP_SECTIONS = {
    "references", "external links", "see also", "notes", "further reading", "bibliography", "sources",
    "citations", "footnotes", "works cited", "notes and references", "gallery", "explanatory notes",
    "general and cited sources", "cited sources", "general sources",
}
DROP_CLASSES = {
    "reference", "mw-ref", "reflist", "references", "navbox", "navbox-styles", "hatnote", "mw-editsection",
    "noprint", "metadata", "ambox", "shortdescription", "sidebar", "thumb", "gallery", "mw-empty-elt",
    "IPA", "rt-commentedText", "infobox", "navigation-not-searchable", "portalbox", "sistersitebox",
    "mw-references-wrap", "toc", "plainlinks", "mw-kartographer-maplink", "box-Multiple_issues",
}
# Link markup. Workers emit OPEN text CLOSE path END; resolve_links() replaces
# the path with the target id (3 bytes) or drops the markup.
LINK_OPEN, LINK_CLOSE, LINK_END = "\x06", "\x07", "\x08"
# A whole raw link, or the leftover half of one whose other half was dropped.
LINK_RAW = re.compile("\x06([^\x06\x07\x08]*)\x07([^\x08]*)\x08|\x07[^\x08]*\x08|\x06")
LINK_ID_LIMIT = 1 << 18
# lxml rejects control characters, so the tree carries private-use stand-ins.
TREE_LINK = str.maketrans({"\ue006": LINK_OPEN, "\ue007": LINK_CLOSE, "\ue008": LINK_END})
EXTRA_FOLD = {"ø": "o", "æ": "ae", "ß": "ss", "đ": "d", "ł": "l", "œ": "oe", "þ": "th", "ð": "d", "ı": "i"}


# --- keys --------------------------------------------------------------------

def norm_key(title):
    """Search key: diacritics folded, lowercase, non-alphanumerics collapsed to one space.

    The Lua app applies the same rule to the query (ASCII part plus EXTRA_FOLD)."""
    s = unicodedata.normalize("NFKD", title).lower()  # NFKD can expand to capitals ("㎾" -> "kW")
    s = "".join(EXTRA_FOLD.get(c, c) for c in s if not unicodedata.combining(c))
    s = re.sub(r"[\W_]+", " ", s).strip()
    return s.encode("utf-8")[:MAX_KEY_BYTES]


# --- HTML -> text ------------------------------------------------------------

def _classes(el):
    return set((el.get("class") or "").split())


def _clean_inline(s):
    s = s.translate(TREE_LINK)
    s = re.sub("[\u200b-\u200f\u2060\ufeff\u00ad]", "", s)  # zero-width, BOM, soft hyphen
    s = re.sub(r"\[(?:\d+|[a-z]|citation needed|note \d+)\]", "", s)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\(\s*[;,]?\s*\)", "", s)        # "( ; )" left behind by dropped IPA
    s = re.sub(r"\(\s*[;,]\s*", "(", s)
    s = re.sub(r"\s+([,.;:])", r"\1", s)
    return s.strip()


SUPERSCRIPT = str.maketrans("0123456789+-=()n", "⁰¹²³⁴⁵⁶⁷⁸⁹⁺⁻⁼⁽⁾ⁿ")


def _drop(el):
    if el.getparent() is not None:
        el.drop_tree()


def _strip_hidden(root):
    """Drop hidden microformat copies and citation markers; keep other superscripts."""
    for el in root.xpath('//*[contains(translate(@style, " ", ""), "display:none")]'):
        _drop(el)
    for el in root.xpath("//sup"):
        text = el.text_content()
        if not text or "reference" in _classes(el) or "mw:Extension/ref" in (el.get("typeof") or "") \
                or text.startswith("["):
            _drop(el)
            continue
        for child in list(el):
            el.remove(child)
        el.text = text.translate(SUPERSCRIPT)


def _mark_links(root):
    """Wraps each article link as OPEN text CLOSE target-path END."""
    for a in root.xpath('//a[@rel="mw:WikiLink"]'):
        href = a.get("href") or ""
        path = urllib.parse.unquote(href[2:] if href.startswith("./") else href).split("#")[0]
        if path and a.text_content().strip():
            a.text = "\ue006" + (a.text or "")
            a.tail = "\ue007" + path + "\ue008" + (a.tail or "")


def strip_links(text):
    return LINK_RAW.sub(lambda m: m.group(1) or "", text)


def encode_id(aid):
    return "".join(chr(0x30 + (aid >> shift & 63)) for shift in (12, 6, 0))


def resolve_links(lines, aid_of_path, self_aid):
    """Turns raw link markup into id links; drops self links, repeats within a
    section, and links to paths outside the pack."""
    seen = set()

    def sub(m):
        if m.group(2) is None:
            return ""
        aid = aid_of_path.get(m.group(2))
        if aid is None or aid == self_aid or aid in seen or not m.group(1).strip():
            return m.group(1)
        seen.add(aid)
        text = m.group(1)
        lead, trail = len(text) - len(text.lstrip()), len(text.rstrip())
        return text[:lead] + LINK_OPEN + text.strip() + LINK_CLOSE + encode_id(aid) + text[trail:]

    out = []
    for line in lines:
        if line.startswith("\x01"):
            seen = set()
            out.append(strip_links(line))
        else:
            out.append(LINK_RAW.sub(sub, line) if "\x06" in line or "\x07" in line else line)
    return out


def _infobox_rows(root):
    rows = []
    for box in root.xpath('//table[contains(concat(" ", normalize-space(@class), " "), " infobox ")]')[:1]:
        for tr in box.iter("tr"):
            th, td = tr.find("th"), tr.find("td")
            if th is None or td is None:
                continue
            for el in td.iter("br", "li", "div", "p"):  # keep list items and lines apart
                el.tail = "; " + (el.tail or "")
            k, v = _clean_inline(th.text_content()), _clean_inline(td.text_content())
            v = re.sub(r"(?:;\s*)+", "; ", v)
            v = re.sub(r"^[;\s]+|[;\s]+$|(?<=:);", "", v)  # also a break right after "Label:"
            if k and v and len(k) < 40 and len(v) < 200:
                rows.append("\x03" + k + "\x1f" + v)
    return rows


def extract(html):
    """Parsoid/Kiwix HTML -> list of text lines (see FORMAT), or [] if not an article."""
    from lxml import html as lhtml

    root = lhtml.fromstring(html)
    if root.xpath('//meta[@property="mw:PageProp/disambiguation"]'):
        return []
    _strip_hidden(root)
    _mark_links(root)
    lines = _infobox_rows(root)
    for el in root.xpath("//script|//style|//link|//meta|//figure|//img|//table|//math|//audio|//video"):
        if el.tag == "math" or "mwe-math-element" in _classes(el.getparent() if el.getparent() is not None else el):
            alt = el.get("alttext")
            if alt:
                el.tail = alt + (el.tail or "")
        _drop(el)
    for el in list(root.iter()):
        if isinstance(el.tag, str) and _classes(el) & DROP_CLASSES:
            _drop(el)
    for el in root.xpath('//span[@typeof="mw:Entity"]'):
        el.drop_tag()

    skip_level = None
    for el in root.iter("h2", "h3", "h4", "p", "li", "dd", "blockquote"):
        if el.tag in ("h2", "h3", "h4"):
            level = int(el.tag[1])
            title = strip_links(_clean_inline(el.text_content()))
            if skip_level is not None and level > skip_level:
                continue
            skip_level = level if title.lower() in DROP_SECTIONS else None
            if skip_level is None and title:
                lines.append("\x01" + str(level) + title)
            continue
        if skip_level is not None:
            continue
        if el.tag == "li" and el.xpath("ancestor::li|ancestor::p"):
            continue                              # nested list: its text is in the parent item
        if el.tag in ("p", "dd") and el.xpath("ancestor::li|ancestor::blockquote"):
            continue
        text = _clean_inline(el.text_content())
        if not text:
            continue
        lines.append(("\x02" if el.tag == "li" else "") + text)
    # Drop headings with no body before the next heading of the same or higher level.
    out = []
    for i, line in enumerate(lines):
        if line.startswith("\x01"):
            nxt = next((l for l in lines[i + 1:] if not l.startswith("\x01") or l[1] <= line[1]), None)
            if nxt is None or nxt.startswith("\x01"):
                continue
        out.append(line)
    return out


# --- sources -----------------------------------------------------------------

class RestSource:
    UA = {"User-Agent": "crosspoint-wikipack/0.1 (offline e-reader sample builder)"}

    def __init__(self, cache):
        self.cache = cache
        os.makedirs(cache, exist_ok=True)

    def _get(self, url):
        for attempt in range(6):
            try:
                with urllib.request.urlopen(urllib.request.Request(url, headers=self.UA), timeout=60) as r:
                    return r.read()
            except urllib.error.HTTPError as e:
                if e.code != 429 or attempt == 5:
                    raise
                wait = int(e.headers.get("Retry-After") or 0) or 5 * 2 ** attempt
                print(f"  rate limited, waiting {wait}s", file=sys.stderr)
                time.sleep(wait)

    def _path(self, title, ext):
        return os.path.join(self.cache, urllib.parse.quote(title, safe="") + ext)

    def article(self, title):
        p = self._path(title, ".html")
        if not os.path.exists(p):
            url = "https://en.wikipedia.org/api/rest_v1/page/html/" + urllib.parse.quote(title.replace(" ", "_"), safe="")
            try:
                data = self._get(url)
            except Exception as e:  # noqa: BLE001 - a missing sample page is not fatal
                print(f"  skip {title}: {e}", file=sys.stderr)
                return None
            with open(p, "wb") as f:
                f.write(data)
            time.sleep(1.0)
        with open(p, "rb") as f:
            return title, f.read().decode("utf-8")

    def redirects(self, titles):
        p = os.path.join(self.cache, "_redirects.json")
        known = json.load(open(p)) if os.path.exists(p) else {}
        todo = [t for t in titles if t not in known]
        for i in range(0, len(todo), 50):
            batch = todo[i:i + 50]
            params = {"action": "query", "prop": "redirects", "rdlimit": "max", "rdnamespace": "0",
                      "titles": "|".join(batch), "format": "json", "formatversion": "2", "redirects": "1"}
            d = json.loads(self._get("https://en.wikipedia.org/w/api.php?" + urllib.parse.urlencode(params)))
            for t in batch:
                known.setdefault(t, [])
            canon = {n["from"]: n["to"] for n in d["query"].get("normalized", []) + d["query"].get("redirects", [])}
            back = {}
            for k, v in canon.items():
                back.setdefault(v, []).append(k)
            for page in d["query"]["pages"]:
                names = [page["title"]] + back.get(page["title"], [])
                for n in names:
                    if n in known:
                        known[n] = [r["title"] for r in page.get("redirects", [])]
        json.dump(known, open(p, "w"))
        return {t: known.get(t, []) for t in titles}


class ZimSource:
    """Kiwix ZIM reader (`pip install libzim`)."""

    def __init__(self, path):
        from libzim.reader import Archive

        self.path = path
        self.arc = Archive(path)

    def _entry(self, title):
        p = title.replace(" ", "_")
        for cand in (p, "A/" + p):
            if self.arc.has_entry_by_path(cand):
                e = self.arc.get_entry_by_path(cand)
                return e.get_redirect_entry() if e.is_redirect else e
        return None

    def article(self, title):
        e = self._entry(title)
        if e is None:
            return None
        return e.title, bytes(e.get_item().content).decode("utf-8", "replace")

    def scan(self):
        """One pass over the directory: ([(entry id, path, title)] for HTML articles,
        {target path: [redirect titles]}, {redirect path: target path})."""
        main = self.arc.main_entry.get_item().path if self.arc.has_main_entry else None
        articles, redirects, redirect_paths = [], {}, {}
        for i in range(self.arc.entry_count):
            e = self.arc._get_entry_by_id(i)  # libzim exposes no public iterator
            if e.is_redirect:
                try:
                    target = e.get_redirect_entry()
                    while target.is_redirect:
                        target = target.get_redirect_entry()
                except RuntimeError:
                    continue
                redirects.setdefault(target.path, []).append(e.title)
                redirect_paths[e.path] = target.path
            elif e.path != main:
                item = e.get_item()
                if item.mimetype != "text/html":
                    continue
                if item.size < STUB_MAX_BYTES:
                    # mwoffliner writes redirects to a section as a page holding one link.
                    m = STUB_LINK.search(bytes(item.content).decode("utf-8", "replace"))
                    if m:
                        target = urllib.parse.unquote(htmllib.unescape(m.group(1)))
                        redirects.setdefault(target, []).append(e.title)
                        redirect_paths[e.path] = target
                        continue
                articles.append((i, e.path, e.title))
        return articles, redirects, redirect_paths

    def redirects(self, titles):
        _, by_path, _ = self.scan()
        return {t: by_path.get(t.replace(" ", "_"), []) for t in titles}


# --- selection ---------------------------------------------------------------

def read_ranking(path, limit):
    titles, weights = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if not parts[0]:
                continue
            titles.append(parts[0].replace("_", " "))
            weights.append(float(parts[1]) if len(parts) > 1 and re.match(r"^[\d.]+$", parts[1]) else None)
    if any(w is not None for w in weights):
        order = sorted(range(len(titles)), key=lambda i: -(weights[i] or 0))
        titles = [titles[i] for i in order]
        weights = [weights[i] or 0 for i in order]
    else:
        weights = [1.0 / (r + 10) for r in range(len(titles))]
    return titles[:limit], weights[:limit]


def line_ends(lines):
    ends, pos = array("I"), 0
    for l in lines:
        pos += len(l.encode("utf-8")) + 1
        ends.append(pos)
    return ends


def take_lines(ends, budget):
    """Number of whole lines to keep for a text budget; the line crossing it is kept."""
    n = 0
    while n < len(ends) and (n == 0 or ends[n - 1] < budget):
        n += 1
    return n


def select(arts, args):
    """arts: list of (weight, ends). Returns lines-to-keep per article (0 = dropped).

    Budgets are in raw bytes; the caller converts using the compression ratio."""
    raw_budget = args.raw_budget
    if args.policy == "full":
        keep, used = [], 0
        for _, ends in arts:
            n = take_lines(ends, args.max_article_bytes)
            size = ends[n - 1] if n else 0
            if used + size > raw_budget:
                break
            keep.append(n)
            used += size
        return keep + [0] * (len(arts) - len(keep))

    t0 = args.t0

    def plan(lam):
        keep, used = [], 0
        for w, ends in arts:
            t = t0 * (w / lam - 1)
            n = take_lines(ends, min(t, args.max_article_bytes)) if t > 0 else 0
            keep.append(n)
            used += ends[n - 1] if n else 0
        return keep, used

    lo, hi = 1e-12, max(w for w, _ in arts) * 2
    for _ in range(60):
        mid = (lo * hi) ** 0.5
        _, used = plan(mid)
        lo, hi = (mid, hi) if used > raw_budget else (lo, mid)  # lo: over budget, hi: fits
    return plan(hi)[0]


# --- packing -----------------------------------------------------------------

def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def body_bytes(lines, truncated=False):
    return "\n".join(lines + (["\x05"] if truncated else [])).encode("utf-8")


class PackWriter:
    """Writes a pack. Titles are fixed up front (ids in rank order); article texts
    may then arrive in any order. With dry_run, text.bin is compressed but not kept."""

    def __init__(self, out, titles, block_raw, page_bytes, dry_run=False):
        if block_raw > 65536:
            raise ValueError("block_raw must be <= 65536")
        os.makedirs(out, exist_ok=True)
        self.out, self.block_raw, self.page_bytes, self.dry_run = out, block_raw, page_bytes, dry_run
        self.count = len(titles)
        self.records = array("I", bytes(16 * self.count))  # block, inner, length, title offset
        with open(self._path("titles.bin"), "wb") as f:
            pos = 0
            for aid, title in enumerate(titles):
                tb = title.encode("utf-8")[:255]
                f.write(bytes([len(tb)]) + tb)
                self.records[aid * 4 + 3] = pos
                pos += 1 + len(tb)
        self.text = open(os.devnull if dry_run else self._path("text.bin"), "wb")
        self.text_size = 0
        self.block_offsets = [0]
        self.pending = bytearray()
        self.stream_bytes = 0
        self.keys = []

    def _path(self, name):
        return os.path.join(self.out, name)

    def _flush(self, final):
        while len(self.pending) >= self.block_raw or (final and self.pending):
            c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
            data = c.compress(bytes(self.pending[:self.block_raw])) + c.flush()
            self.text.write(data)
            self.text_size += len(data)
            if self.text_size >= 2 ** 31:
                raise ValueError("text.bin would exceed 2GB, the Lua integer range")
            self.block_offsets.append(self.text_size)
            del self.pending[:self.block_raw]

    def add_text(self, aid, body):
        block, inner = divmod(self.stream_bytes, self.block_raw)
        self.records[aid * 4:aid * 4 + 3] = array("I", (block, inner, len(body)))
        self.pending += body
        self.stream_bytes += len(body)
        self._flush(False)

    def add_keys(self, aid, title, redirects):
        k = norm_key(title)
        seen = {k}
        self.keys.append((k, aid, 0))
        for r in redirects:
            rk = norm_key(r)
            if rk and rk not in seen:
                seen.add(rk)
                self.keys.append((rk, aid, 1))

    def finish(self):
        self._flush(True)
        self.text.close()
        with open(self._path("blocks.bin"), "wb") as f:
            f.write(struct.pack(f"<{len(self.block_offsets)}I", *self.block_offsets))
        with open(self._path("articles.bin"), "wb") as f:
            f.write(self.records.tobytes())  # host is little-endian (checked in main)

        present = [k for k in self.keys if k[0] and self.records[k[1] * 4 + 2]]
        present.sort()
        pages, firsts, page, prev = [], [], bytearray(), b""
        for k, aid, redirect in present:
            tail = varint(aid << 1 | redirect)
            shared = 0
            if page:
                # Suffix length 0 ends a page, so a repeated key re-sends its last byte.
                while shared < min(len(k) - 1, len(prev), 255) and k[shared] == prev[shared]:
                    shared += 1
                if len(page) + 2 + len(k) - shared + len(tail) + 2 > self.page_bytes:
                    pages.append(bytes(page))
                    page, shared = bytearray(), 0
            if not page:
                firsts.append(k)
            page += bytes([shared, len(k) - shared]) + k[shared:] + tail
            prev = k
        if page:
            pages.append(bytes(page))
        with open(self._path("index.bin"), "wb") as f:
            for p in pages:
                f.write(p + bytes(self.page_bytes - len(p)))
        with open(self._path("pages.bin"), "wb") as f:
            for k in firsts:
                f.write(k[:KEY_PREFIX_BYTES].ljust(KEY_PREFIX_BYTES, b"\0"))

        nonempty = sum(1 for aid in range(self.count) if self.records[aid * 4 + 2])
        hdr = {"version": FORMAT_VERSION, "articles": self.count, "nonempty": nonempty, "keys": len(present),
               "blocks": len(self.block_offsets) - 1, "block_raw": self.block_raw, "page_bytes": self.page_bytes,
               "pages": len(pages), "key_prefix": KEY_PREFIX_BYTES, "stream_bytes": self.stream_bytes,
               "built": time.strftime("%Y-%m-%d")}
        with open(self._path("wiki.hdr"), "w") as f:
            f.write("".join(f"{k}={v}\n" for k, v in hdr.items()))
        sizes = {n: os.path.getsize(self._path(n)) for n in os.listdir(self.out) if n != "text.bin"}
        sizes["text.bin"] = self.text_size
        return hdr, sizes


def report(hdr, sizes, out):
    total = sum(sizes.values())
    print(f"{hdr['nonempty']} articles with text (of {hdr['articles']}), {hdr['keys']} search keys, "
          f"{hdr['stream_bytes'] / 1e6:.1f} MB text -> {sizes['text.bin'] / 1e6:.1f} MB "
          f"(ratio {hdr['stream_bytes'] / max(1, sizes['text.bin']):.2f})")
    for name in sorted(sizes, key=lambda n: -sizes[n]):
        print(f"  {name:13} {sizes[name] / 1e6:10.2f} MB")
    print(f"  {'total':13} {total / 1e6:10.2f} MB ({total / 2 ** 20:.1f} MiB) -> {out}")


# --- whole-ZIM mode ----------------------------------------------------------

_worker_arc = None
_worker_links = None  # {path: article id} for link resolution
_worker_aids = None   # {entry id: article id}


def _worker_init(path, links, aids):
    global _worker_arc, _worker_links, _worker_aids
    from libzim.reader import Archive

    _worker_arc = Archive(path)
    _worker_links, _worker_aids = links, aids


def _worker_extract(entry_ids):
    out = []
    for i in entry_ids:
        entry = _worker_arc._get_entry_by_id(i)
        try:
            lines = extract(bytes(entry.get_item().content).decode("utf-8", "replace"))
        except Exception as e:  # noqa: BLE001 - one bad page must not stop a 40-minute run
            print(f"  skip {entry.path}: {type(e).__name__}: {e}", file=sys.stderr)
            lines = []
        lines = resolve_links(lines, _worker_links, _worker_aids[i]) if lines else lines
        body = body_bytes(lines) if lines and sum(len(l) for l in lines) >= MIN_ARTICLE_BYTES else b""
        out.append((i, body))
    return out


def build_all(args):
    """Every HTML article of a ZIM, untruncated, streamed straight into the pack."""
    import multiprocessing

    src = ZimSource(args.zim)
    t = time.time()
    articles, redirects, redirect_paths = src.scan()
    print(f"scan: {len(articles)} articles, {sum(map(len, redirects.values()))} redirects "
          f"({time.time() - t:.0f}s)", file=sys.stderr)
    rank = {}
    if args.ranking:
        titles, _ = read_ranking(args.ranking, args.limit)
        rank = {tt.replace(" ", "_"): r for r, tt in enumerate(titles)}
    if args.max_articles:
        articles.sort(key=lambda a: (rank.get(a[1], len(rank)), a[0]))
        articles = articles[:args.max_articles]
    articles.sort(key=lambda a: (rank.get(a[1], len(rank)), a[0]))  # ids: ranked first, then ZIM order
    aid_of = {entry: aid for aid, (entry, _, _) in enumerate(articles)}
    writer = PackWriter(args.out, [title for _, _, title in articles], args.block_raw, args.page_bytes,
                        args.dry_run)
    for aid, (_, path, title) in enumerate(articles):
        writer.add_keys(aid, title, redirects.get(path, []))
    del redirects
    if len(articles) > LINK_ID_LIMIT:
        raise ValueError(f"links address at most {LINK_ID_LIMIT} articles")
    links = {path: aid for aid, (_, path, _) in enumerate(articles)}
    for rpath, target in redirect_paths.items():
        if target in links and rpath not in links:
            links[rpath] = links[target]
    del redirect_paths

    # Extract in ZIM order so each worker reads its clusters sequentially.
    by_entry = sorted(aid_of)
    chunks = [by_entry[i:i + args.chunk] for i in range(0, len(by_entry), args.chunk)]
    done, t = 0, time.time()
    with multiprocessing.Pool(args.jobs, _worker_init, (args.zim, links, aid_of)) as pool:
        for result in pool.imap(_worker_extract, chunks):
            for entry, body in result:
                if body:
                    writer.add_text(aid_of[entry], body)
            done += len(result)
            if done % (args.chunk * 50) < args.chunk or done == len(by_entry):
                rate = done / max(1e-6, time.time() - t)
                print(f"  {done}/{len(by_entry)} articles, {writer.stream_bytes / 1e6:.0f} MB text, "
                      f"{writer.text_size / 1e6:.0f} MB compressed, {rate:.0f}/s, "
                      f"eta {(len(by_entry) - done) / max(rate, 1e-6) / 60:.0f} min", file=sys.stderr)
    hdr, sizes = writer.finish()
    report(hdr, sizes, args.out)


# --- budgeted mode -----------------------------------------------------------

def build_budgeted(args):
    source = RestSource(args.rest_cache) if args.rest_cache else ZimSource(args.zim)
    titles, weights = read_ranking(args.ranking, args.limit)
    print(f"ranking: {len(titles)} titles", file=sys.stderr)

    extracted = []  # (weight, title, lines)
    for i, (t, w) in enumerate(zip(titles, weights)):
        got = source.article(t)
        if got is None:
            continue
        real_title, html = got
        lines = extract(html)
        if lines and sum(len(l) for l in lines) >= MIN_ARTICLE_BYTES:
            extracted.append((w, real_title, lines))
        if i % 1000 == 999:
            print(f"  extracted {i + 1}", file=sys.stderr)
    redirects = source.redirects([t for _, t, _ in extracted])

    arts = [(w, line_ends(lines)) for w, _, lines in extracted]
    budget = args.budget * 1024 * 1024
    ratio = args.ratio
    for attempt in range(3):
        # Fixed per-article cost: articles.bin record + title; index estimated at 10 B/key.
        args.raw_budget = budget * ratio
        keep = select(arts, args)
        overhead = sum(16 + 1 + len(extracted[i][1].encode()) + 10 * (1 + len(redirects[extracted[i][1]]))
                       for i, n in enumerate(keep) if n)
        args.raw_budget = (budget - overhead) * ratio
        keep = select(arts, args)
        chosen = [(t, lines[:n], n < len(lines)) for (w, t, lines), n in zip(extracted, keep) if n]
        writer = PackWriter(args.out, [t for t, _, _ in chosen], args.block_raw, args.page_bytes, args.dry_run)
        links = {}
        for aid, (t, _, _) in enumerate(chosen):
            for name in [t] + redirects[t]:
                links.setdefault(name.replace(" ", "_"), aid)
        for aid, (t, lines, truncated) in enumerate(chosen):
            writer.add_text(aid, body_bytes(resolve_links(lines, links, aid), truncated))
            writer.add_keys(aid, t, redirects[t])
        hdr, sizes = writer.finish()
        size = sum(sizes.values())
        actual = hdr["stream_bytes"] / max(1, sizes["text.bin"])
        print(f"pass {attempt + 1}: {hdr['articles']} articles, {size / 1e6:.2f} MB (ratio {actual:.2f})",
              file=sys.stderr)
        if size <= budget and abs(actual - ratio) < 0.05:
            break
        ratio = actual * 0.995
    report(hdr, sizes, args.out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--rest-cache", help="fetch HTML from the Wikipedia REST API into this cache dir")
    src.add_argument("--zim", help="Kiwix ZIM file")
    ap.add_argument("--ranking", help="titles, most popular first (required unless --all)")
    ap.add_argument("--limit", type=int, default=1_000_000, help="consider at most this many ranked titles")
    ap.add_argument("--out", required=True)
    ap.add_argument("--all", action="store_true", help="every article of the ZIM, untruncated (no budget)")
    ap.add_argument("--max-articles", type=int, help="--all: only the first N (by rank, then ZIM order)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--chunk", type=int, default=64, help="--all: articles per worker task")
    ap.add_argument("--dry-run", action="store_true", help="compress but do not write text.bin")
    ap.add_argument("--budget", type=float, default=100, help="total pack size in MiB (default 100)")
    ap.add_argument("--policy", choices=("full", "waterfill"), default="waterfill")
    ap.add_argument("--t0", type=float, default=2000, help="waterfill: bytes after which value halves")
    ap.add_argument("--max-article-bytes", type=int, default=200_000)
    ap.add_argument("--block-raw", type=int, default=64 * 1024)
    ap.add_argument("--page-bytes", type=int, default=4096)
    ap.add_argument("--ratio", type=float, default=2.5, help="initial deflate ratio estimate")
    args = ap.parse_args()
    if sys.byteorder != "little":
        sys.exit("articles.bin is written in host byte order; run on a little-endian host")
    if args.all:
        if not args.zim:
            ap.error("--all needs --zim")
        build_all(args)
    else:
        if not args.ranking:
            ap.error("--ranking is required without --all")
        build_budgeted(args)


if __name__ == "__main__":
    main()
