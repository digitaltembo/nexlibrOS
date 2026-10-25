#!/usr/bin/env python3
"""Write a raw SD card image: an MBR with one partition holding a FAT16 or FAT32 filesystem of the
given files, long names included. Standard library only, so it works the same on every OS.

    python3 scripts/fat_image.py OUT.img SRC_DIR [SIZE_MB]

SRC_DIR's contents become the card's root. Without SIZE_MB the card is sized to fit (32 MB at
least). Cards up to 2 GB are FAT16, larger ones FAT32. Layout and field names follow Microsoft's
FAT specification (fatgen103.doc): the BPB, the FAT size computation, long-name entries and their
checksum. The output is reproducible: every timestamp and the volume ID are fixed.
"""

import hashlib
import math
import shutil
import struct
import sys
from pathlib import Path

SECTOR = 512
PART_START = 2048                      # the partition starts at 1 MiB, as SD formatters align it
MIN_SIZE = 32 << 20
STAMP_DATE = (2026 - 1980) << 9 | 1 << 5 | 1      # 2026-01-01
STAMP_TIME = 12 << 11                              # 12:00:00
SHORT_OK = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")
SKIP = {".DS_Store", "Thumbs.db"}


class Node:
    def __init__(self, name, path=None, children=None):
        self.name, self.path, self.children = name, path, children
        self.size = path.stat().st_size if path else 0
        self.cluster = self.clusters = 0
        self.short = b""

    @property
    def is_dir(self):
        return self.children is not None


def tree(src):
    """The directory at src as Nodes, sorted by name; editor and OS litter is left out."""
    kids = []
    for p in sorted(src.iterdir(), key=lambda p: p.name):
        if p.name in SKIP:
            continue
        kids.append(Node(p.name, children=tree(p).children) if p.is_dir() else Node(p.name, path=p))
    return Node("", children=kids)


def short_name(name, taken):
    """The 8.3 name, and whether the long name needs LFN entries (any other spelling does)."""
    up = name.upper()
    if name == up and name not in (".", ".."):
        base, _, ext = up.partition(".")
        if 0 < len(base) <= 8 and len(ext) <= 3 and "." not in ext and set(base + ext) <= SHORT_OK:
            short = base.ljust(8).encode() + ext.ljust(3).encode()
            if short not in taken:
                return short, False
    stem = up.lstrip(".")
    base, ext = stem.rsplit(".", 1) if "." in stem else (stem, "")
    clean = lambda s: "".join(c if c in SHORT_OK else "_" for c in s.replace(" ", "").replace(".", ""))
    base, ext = clean(base) or "_", clean(ext)[:3]
    for n in range(1, 1000000):
        tail = f"~{n}"
        short = (base[:8 - len(tail)] + tail).ljust(8).encode() + ext.ljust(3).encode()
        if short not in taken:
            return short, True
    raise ValueError(f"no short name left for {name}")


def checksum(short):
    s = 0
    for b in short:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
    return s


def lfn_entries(name, short):
    units = [name.encode("utf-16-le")[i:i + 2] for i in range(0, len(name.encode("utf-16-le")), 2)]
    if len(units) % 13:
        units.append(b"\0\0")
        units += [b"\xff\xff"] * (-len(units) % 13)
    count, chk, out = len(units) // 13, checksum(short), []
    for i in range(count, 0, -1):          # the last part comes first on disk
        part = units[(i - 1) * 13:i * 13]
        out.append(bytes([i | (0x40 if i == count else 0)]) + b"".join(part[:5]) + bytes([0x0F, 0, chk])
                   + b"".join(part[5:11]) + b"\0\0" + b"".join(part[11:]))
    return out


def entry(short, attr, cluster, size):
    return struct.pack("<11sBBBHHHHHHHI", short, attr, 0, 0, STAMP_TIME, STAMP_DATE, STAMP_DATE,
                       cluster >> 16, STAMP_TIME, STAMP_DATE, cluster & 0xFFFF, size)


def dir_entries(node, parent_cluster, label=None):
    """The 32-byte entries of a directory (its children's clusters must already be assigned)."""
    out = []
    if label is not None:
        out.append(entry(label.upper().ljust(11)[:11].encode(), 0x08, 0, 0))
    else:
        out.append(entry(b".".ljust(11), 0x10, node.cluster, 0))
        out.append(entry(b"..".ljust(11), 0x10, parent_cluster, 0))
    taken = set()
    for kid in node.children:
        short, long = short_name(kid.name, taken)
        taken.add(short)
        kid.short = short
        if long:
            out += lfn_entries(kid.name, short)
        out.append(entry(short, 0x10 if kid.is_dir else 0x20, kid.cluster, 0 if kid.is_dir else kid.size))
    return out


def entry_count(node, root):
    taken, n = set(), 1 if root else 2
    for kid in node.children:
        short, long = short_name(kid.name, taken)
        taken.add(short)
        n += 1 + (math.ceil(len(kid.name.encode("utf-16-le")) // 2 / 13) if long else 0)
    return n


def geometry(total_bytes):
    """(fat bits, sectors per cluster, reserved sectors, root dir sectors, sectors per FAT, clusters)."""
    sectors = total_bytes // SECTOR - PART_START
    for bits in (16, 32):
        rsvd, root = (1, 32) if bits == 16 else (32, 0)
        for spc in (1, 2, 4, 8, 16, 32, 64):
            # FAT32 cluster sizes by volume size: 4 KB to 8 GB, 8 KB to 16 GB, 16 KB to 32 GB, then 32 KB
            if bits == 32 and spc < (8 if sectors <= 1 << 24 else 16 if sectors <= 1 << 25 else 32 if sectors <= 1 << 26 else 64):
                continue
            tmp2 = 256 * spc + 2 if bits == 16 else (256 * spc + 2) // 2
            fatsz = math.ceil((sectors - rsvd - root) / tmp2)
            clusters = (sectors - rsvd - 2 * fatsz - root) // spc
            if (bits == 16 and 4085 <= clusters <= 65524) or (bits == 32 and clusters >= 65525):
                return bits, spc, rsvd, root, fatsz, clusters
    raise ValueError(f"cannot lay out a FAT16 or FAT32 volume in {total_bytes} bytes")


def assign(root, bits, cbytes):
    """Give every directory and file its run of clusters, depth first. Returns the next free cluster."""
    nxt = 2

    def walk(node, is_root):
        nonlocal nxt
        if not (is_root and bits == 16):        # the FAT16 root is a fixed region, not clusters
            node.clusters = max(1, math.ceil(entry_count(node, is_root) * 32 / cbytes))
            node.cluster, nxt = nxt, nxt + node.clusters
        for kid in node.children:
            if kid.is_dir:
                walk(kid, False)
            elif kid.size:
                kid.clusters = math.ceil(kid.size / cbytes)
                kid.cluster, nxt = nxt, nxt + kid.clusters
    walk(root, True)
    return nxt


def needed_bytes(root):
    """A size that holds the tree with room to spare: the next MB above twice the content."""
    content = sum(math.ceil(n.size / 4096) * 4096 + 4096 for n in walk_nodes(root))
    return max(MIN_SIZE, (2 * content + (8 << 20)) >> 20 << 20)


def walk_nodes(node):
    for kid in node.children:
        yield kid
        if kid.is_dir:
            yield from walk_nodes(kid)


def write(out, src, size=None, label="NEXLIBROS"):
    """Write the card image of directory src to the file out; returns (fat bits, image bytes)."""
    root = tree(Path(src))
    size = size or needed_bytes(root)
    bits, spc, rsvd, root_secs, fatsz, clusters = geometry(size)
    cbytes = spc * SECTOR
    end = assign(root, bits, cbytes)
    if end - 2 > clusters:
        raise ValueError(f"{src} does not fit in {size >> 20} MB")
    if bits == 16 and entry_count(root, True) > 512:
        raise ValueError("more than 512 root directory entries (long names count) on a FAT16 card")
    part_sectors = size // SECTOR - PART_START
    first_data = rsvd + 2 * fatsz + root_secs
    data_at = lambda c: (PART_START + first_data) * SECTOR + (c - 2) * cbytes
    vol_id = int.from_bytes(hashlib.sha256(label.encode()).digest()[:4], "little")

    with open(out, "wb") as f:
        f.truncate(size)
        # MBR: one partition, FAT16 or FAT32 with LBA addressing
        part = struct.pack("<B3sB3sII", 0, b"\x20\x21\x00", 0x0E if bits == 16 else 0x0C, b"\xfe\xff\xff", PART_START, part_sectors)
        f.write(bytes(446) + part + bytes(48) + b"\x55\xaa")
        # boot sector
        bpb = struct.pack("<3s8sHBHBHHBHHHII", b"\xeb\x3c\x90" if bits == 16 else b"\xeb\x58\x90", b"MSWIN4.1", SECTOR, spc, rsvd, 2,
                          512 if bits == 16 else 0, part_sectors if bits == 16 and part_sectors < 0x10000 else 0, 0xF8,
                          fatsz if bits == 16 else 0, 63, 255, PART_START, 0 if bits == 16 and part_sectors < 0x10000 else part_sectors)
        tail = struct.pack("<BBBI11s8s", 0x80, 0, 0x29, vol_id, label.upper().ljust(11)[:11].encode(), b"FAT16   " if bits == 16 else b"FAT32   ")
        if bits == 32:
            tail = struct.pack("<IHHIHH12s", fatsz, 0, 0, root.cluster, 1, 6, bytes(12)) + tail
        boot = (bpb + tail).ljust(510, b"\0") + b"\x55\xaa"
        f.seek(PART_START * SECTOR)
        f.write(boot)
        if bits == 32:
            fsinfo = struct.pack("<I480sII", 0x41615252, bytes(480), 0x61417272, clusters - (end - 2)) + struct.pack("<I12sI", end, bytes(12), 0xAA550000)
            for at in (0, 6):                   # the boot sector and FSInfo, and their backups at sector 6
                f.seek((PART_START + at) * SECTOR)
                f.write(boot + fsinfo)
        # the FAT: every file and directory is one contiguous run
        width, eoc = (2, 0xFFFF) if bits == 16 else (4, 0x0FFFFFFF)
        fat = bytearray(fatsz * SECTOR)
        put = lambda i, v: fat.__setitem__(slice(i * width, (i + 1) * width), v.to_bytes(width, "little"))
        put(0, 0xFFF8 if bits == 16 else 0x0FFFFFF8)
        put(1, eoc)
        for node in [root, *walk_nodes(root)]:
            for i in range(node.clusters):
                put(node.cluster + i, eoc if i == node.clusters - 1 else node.cluster + i + 1)
        for copy in range(2):
            f.seek((PART_START + rsvd + copy * fatsz) * SECTOR)
            f.write(fat)

        def write_dir(node, parent_cluster, is_root):
            data = b"".join(dir_entries(node, parent_cluster, label if is_root else None))
            f.seek((PART_START + rsvd + 2 * fatsz) * SECTOR if is_root and bits == 16 else data_at(node.cluster))
            f.write(data)
            here = 0 if is_root else node.cluster      # ".." of a top-level directory names the root as 0
            for kid in node.children:
                if kid.is_dir:
                    write_dir(kid, here, False)
                elif kid.size:
                    f.seek(data_at(kid.cluster))
                    with open(kid.path, "rb") as src_file:
                        shutil.copyfileobj(src_file, f, 1 << 20)
        write_dir(root, 0, True)
    return bits, size


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__.split("\n\n")[1])
    out, src = sys.argv[1], Path(sys.argv[2])
    if not src.is_dir():
        sys.exit(f"fat_image: not a directory: {src}")
    try:
        bits, size = write(out, src, int(sys.argv[3]) << 20 if len(sys.argv) == 4 else None)
    except ValueError as e:
        sys.exit(f"fat_image: {e}")
    print(f"{out}: {size >> 20} MB, FAT{bits}")


if __name__ == "__main__":
    main()
