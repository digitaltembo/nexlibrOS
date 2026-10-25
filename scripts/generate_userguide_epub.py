#!/usr/bin/env python3
"""Generate an EPUB from USER_GUIDE.md, optionally followed by everything in docs/.

Usage:
  python3 scripts/generate_userguide_epub.py           # -> nexlibrOS_User_Guide.epub
  python3 scripts/generate_userguide_epub.py --docs    # -> nexlibrOS_Documentation.epub

Requires: markdown, lxml, Pillow.
"""

import argparse
import html as _html
import io
import re
import zipfile
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

import markdown
from lxml import etree
from lxml import html as lhtml
from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parent.parent
SOURCE_MD = ROOT / "USER_GUIDE.md"
DOCS_DIR = ROOT / "docs"
OUTPUT_GUIDE = ROOT / "nexlibrOS_User_Guide.epub"
OUTPUT_DOCS = ROOT / "nexlibrOS_Documentation.epub"
COVER_ART = ROOT / "docs/images/userguide-cover.png"

# Portrait cover dimensions matching the X4 display (480×800)
COVER_W, COVER_H = 480, 800
# Embedded images are converted to grayscale baseline JPEGs that fit this box
IMAGE_MAX = (800, 800)

FONT_CANDIDATES = [
    ('/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf',
     '/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf'),
    ('/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf',
     '/System/Library/Fonts/Supplemental/Times New Roman.ttf'),
]

CSS = """
body {
    font-family: Georgia, 'Times New Roman', serif;
    font-size: 1em;
    line-height: 1.6;
    margin: 1em 1.5em;
    color: #111;
}

h1 { font-size: 1.8em; margin-top: 1.2em; margin-bottom: 0.4em; }
h2 { font-size: 1.4em; margin-top: 1.5em; margin-bottom: 0.3em; border-bottom: 1px solid #ccc; padding-bottom: 0.2em; }
h3 { font-size: 1.2em; margin-top: 1.2em; margin-bottom: 0.2em; }
h4 { font-size: 1.05em; margin-top: 1em; margin-bottom: 0.2em; }
h5 { font-size: 1em; margin-top: 0.8em; margin-bottom: 0.2em; }

p { margin: 0.6em 0; }

code {
    font-family: 'Courier New', Courier, monospace;
    font-size: 0.85em;
    background: #f4f4f4;
    padding: 0.1em 0.3em;
    border-radius: 3px;
}

pre {
    font-family: 'Courier New', Courier, monospace;
    font-size: 0.8em;
    background: #f4f4f4;
    border: 1px solid #ddd;
    border-radius: 4px;
    padding: 0.8em 1em;
    overflow-x: auto;
    white-space: pre-wrap;
    word-wrap: break-word;
}

pre code {
    background: none;
    padding: 0;
    border-radius: 0;
    font-size: 1em;
}

table {
    border-collapse: collapse;
    width: 100%;
    margin: 0.8em 0;
    font-size: 0.9em;
}

th, td {
    border: 1px solid #ccc;
    padding: 0.4em 0.6em;
    text-align: left;
}

th { background: #eee; font-weight: bold; }

ul, ol { margin: 0.5em 0; padding-left: 1.8em; }
li { margin: 0.25em 0; }

a { color: #1a6699; text-decoration: none; }

hr { border: none; border-top: 1px solid #ccc; margin: 1.5em 0; }

img { max-width: 100%; height: auto; }

.callout {
    border-left: 4px solid #888;
    background: #f8f8f8;
    padding: 0.5em 0.8em;
    margin: 0.8em 0;
    border-radius: 0 4px 4px 0;
}

.callout-note, .callout-important { border-color: #2196F3; background: #e3f2fd; }
.callout-tip    { border-color: #4CAF50; background: #e8f5e9; }
.callout-warning, .callout-caution { border-color: #FF9800; background: #fff3e0; }

.callout-title {
    font-weight: bold;
    margin-bottom: 0.3em;
}

/* Cover page */
.cover-page {
    text-align: center;
    margin: 0;
    padding: 0;
}

.cover-page img {
    width: 100%;
    height: auto;
    display: block;
    margin: 0;
}
"""


@dataclass
class Chapter:
    file_name: str
    title: str
    source: Path
    html: str
    body: object = None  # parsed lxml element, set by build_epub


@dataclass
class TocEntry:
    title: str
    href: str
    children: list = field(default_factory=list)


def load_fonts():
    for bold, regular in FONT_CANDIDATES:
        try:
            return ImageFont.truetype(bold, 52), ImageFont.truetype(regular, 32)
        except OSError:
            continue
    default = ImageFont.load_default()
    return default, default


def make_cover_png(subtitle: str) -> bytes:
    """Generate a full-size (480×800) cover: the artwork with the title and subtitle above it."""
    # The artwork fills the screen and leaves its upper part blank for the text
    with Image.open(COVER_ART) as art:
        cover = art.convert('RGB').resize((COVER_W, COVER_H), Image.LANCZOS)
    draw = ImageDraw.Draw(cover)

    font_title, font_sub = load_fonts()
    title = 'nexlibrOS'

    # Draw title text
    bbox = draw.textbbox((0, 0), title, font=font_title)
    tw = bbox[2] - bbox[0]
    ty = int(COVER_H * 0.30)
    draw.text(((COVER_W - tw) // 2, ty), title, fill=(0, 0, 0), font=font_title)

    # Draw subtitle text
    bbox2 = draw.textbbox((0, 0), subtitle, font=font_sub)
    sw = bbox2[2] - bbox2[0]
    sy = ty + (bbox[3] - bbox[1]) + 18
    draw.text(((COVER_W - sw) // 2, sy), subtitle, fill=(80, 80, 80), font=font_sub)

    # Thin horizontal rules above and below the text block
    rule_y1 = ty - 24
    rule_y2 = sy + (bbox2[3] - bbox2[1]) + 24
    draw.line([(60, rule_y1), (COVER_W - 60, rule_y1)], fill=(180, 180, 180), width=1)
    draw.line([(60, rule_y2), (COVER_W - 60, rule_y2)], fill=(180, 180, 180), width=1)

    buf = io.BytesIO()
    cover.save(buf, format='PNG')
    return buf.getvalue()


def convert_image(path: Path) -> bytes:
    """Shrink an image to fit the display as a grayscale baseline JPEG."""
    with Image.open(path) as im:
        im = ImageOps.exif_transpose(im).convert('L')
        im.thumbnail(IMAGE_MAX, Image.LANCZOS)
        buf = io.BytesIO()
        im.save(buf, format='JPEG', quality=80, optimize=True)
        return buf.getvalue()


def github_slug(value: str, separator: str) -> str:
    """Heading ids matching GitHub's, so `file.md#anchor` links resolve."""
    value = re.sub(r'[^\w\- ]', '', value.strip().lower())
    return value.replace(' ', separator)


def render_markdown(text: str, with_ids: bool = True) -> str:
    extensions = ['tables', 'fenced_code', 'attr_list']
    configs = {}
    if with_ids:
        extensions.append('toc')
        configs['toc'] = {'slugify': github_slug}
    return markdown.markdown(text, extensions=extensions, extension_configs=configs,
                             output_format='xhtml')


def preprocess_callouts(text: str) -> str:
    """Convert GitHub-style > [!TYPE] callouts to HTML divs."""
    lines = text.splitlines()
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = re.match(r'^>\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*$', line.strip())
        if m:
            kind = m.group(1).lower()
            title = kind.capitalize()
            body_lines = []
            i += 1
            while i < len(lines) and lines[i].startswith('>') \
                    and not re.match(r'^>\s*\[!', lines[i]):
                body_lines.append(lines[i][1:].lstrip())
                i += 1
            body = render_markdown('\n'.join(body_lines).strip(), with_ids=False)
            out.append(
                f'\n<div class="callout callout-{kind}">'
                f'<div class="callout-title">{title}</div>'
                f'{body}'
                f'</div>\n'
            )
        else:
            out.append(line)
            i += 1
    return '\n'.join(out)


def strip_toc(text: str) -> str:
    """Remove the inline TOC: the first top-level `- [Title](#anchor)` item and its nested items."""
    return re.sub(r'\n- \[[^\]]+\]\(#[^)]+\)\n(?:[ \t]+- .*\n)*', '\n', text, count=1)


def split_chapters(html: str):
    """Split rendered HTML into (anchor_id, title, html_fragment) tuples by H2."""
    pattern = re.compile(r'(<h2[^>]*>)(.*?)(</h2>)', re.DOTALL)
    chapters = []
    positions = [(m.start(), m.group(0), m.group(2)) for m in pattern.finditer(html)]

    if not positions:
        return [('intro', 'nexlibrOS User Guide', html)]

    intro_html = html[:positions[0][0]].strip()
    if intro_html:
        chapters.append(('intro', 'Introduction', intro_html))

    seen_anchors = {c[0] for c in chapters}  # seed with 'intro' if present
    for idx, (start, _tag, raw_title) in enumerate(positions):
        end = positions[idx + 1][0] if idx + 1 < len(positions) else len(html)
        fragment = html[start:end].strip()
        title = _html.unescape(re.sub(r'<[^>]+>', '', raw_title).strip())
        base = re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-') or 'section'
        anchor = base
        n = 1
        while anchor in seen_anchors:
            anchor = f'{base}-{n}'
            n += 1
        seen_anchors.add(anchor)
        chapters.append((anchor, title, fragment))

    return chapters


def user_guide_chapters():
    source = SOURCE_MD.read_text(encoding='utf-8')
    source = preprocess_callouts(strip_toc(source))
    return [Chapter(f'chap_{anchor}.xhtml', title, SOURCE_MD, fragment)
            for anchor, title, fragment in split_chapters(render_markdown(source))]


def doc_chapter(path: Path) -> Chapter:
    body = render_markdown(preprocess_callouts(path.read_text(encoding='utf-8')))
    m = re.search(r'<h1[^>]*>(.*?)</h1>', body, re.DOTALL)
    title = _html.unescape(re.sub(r'<[^>]+>', '', m.group(1)).strip()) if m else path.stem
    rel = path.relative_to(DOCS_DIR).with_suffix('')
    slug = re.sub(r'[^a-z0-9]+', '-', str(rel).lower()).strip('-')
    return Chapter(f'doc_{slug}.xhtml', title, path, body)


def docs_groups():
    """(group title, chapters) for docs/*.md, then docs/contributing/ (README first)."""
    contributing = DOCS_DIR / 'contributing'
    readme = contributing / 'README.md'
    contrib_paths = ([readme] if readme.exists() else []) + \
        sorted(p for p in contributing.glob('*.md') if p != readme)
    groups = [
        ('Documentation', [doc_chapter(p) for p in sorted(DOCS_DIR.glob('*.md'))]),
        ('Contributing', [doc_chapter(p) for p in contrib_paths]),
    ]
    return [(title, chapters) for title, chapters in groups if chapters]


def is_external(url: str) -> bool:
    return bool(re.match(r'^[a-z][a-z0-9+.-]*:', url, re.IGNORECASE))


def rewrite_links(chapter: Chapter, targets: dict):
    """Point links at other bundled markdown files to their chapter files.

    Links to anything not in the book (source files, SCOPE.md, ...) are unwrapped
    to plain text since they can't be followed on the device.
    """
    for a in list(chapter.body.iter('a')):
        href = a.get('href')
        if not href or is_external(href):
            continue
        path_part, _, frag = href.partition('#')
        target = (chapter.source.parent / path_part).resolve() if path_part else chapter.source
        doc = targets.get(target)
        if doc is None:
            a.drop_tag()
            continue
        first_file, ids = doc
        if frag and frag in ids:
            a.set('href', f'{ids[frag]}#{frag}')
        else:
            a.set('href', first_file)


def collect_images(chapter: Chapter, images: dict):
    """Embed local images (converted for e-ink); drop remote ones the reader can't fetch."""
    for img in list(chapter.body.iter('img')):
        src = img.get('src', '')
        path = None if is_external(src) else (chapter.source.parent / src).resolve()
        if path is None or not path.is_file():
            print(f'  Skipping image {src!r} in {chapter.source.relative_to(ROOT)}')
            img.drop_tree()
            continue
        if path not in images:
            name = re.sub(r'[^A-Za-z0-9]+', '-', str(path.relative_to(ROOT).with_suffix('')))
            images[path] = (f'images/{name}.jpg', convert_image(path))
        img.set('src', images[path][0])
        img.attrib.pop('width', None)


def make_xhtml(title: str, body_xml: str) -> str:
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en">\n'
        f'<head><title>{_html.escape(title)}</title>\n'
        '<link rel="stylesheet" type="text/css" href="style/main.css"/>\n'
        f'</head>\n{body_xml}\n</html>\n'
    )


def make_nav(toc) -> str:
    def items(entries):
        out = ['<ol>']
        for e in entries:
            out.append(f'<li><a href="{_html.escape(e.href)}">{_html.escape(e.title)}</a>')
            if e.children:
                out.append(items(e.children))
            out.append('</li>')
        out.append('</ol>')
        return '\n'.join(out)

    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<!DOCTYPE html>\n'
        '<html xmlns="http://www.w3.org/1999/xhtml" '
        'xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="en" lang="en">\n'
        '<head><title>Contents</title></head>\n<body>\n'
        f'<nav epub:type="toc" id="toc"><h1>Contents</h1>\n{items(toc)}\n</nav>\n'
        '</body>\n</html>\n'
    )


def make_ncx(identifier: str, title: str, toc) -> str:
    play_orders = {}
    counter = [0]

    def points(entries, depth):
        out = []
        for e in entries:
            counter[0] += 1
            order = play_orders.setdefault(e.href, len(play_orders) + 1)
            pad = '  ' * depth
            out.append(
                f'{pad}<navPoint id="np{counter[0]}" playOrder="{order}">'
                f'<navLabel><text>{_html.escape(e.title)}</text></navLabel>'
                f'<content src="{_html.escape(e.href)}"/>'
            )
            out.extend(points(e.children, depth + 1))
            out.append(f'{pad}</navPoint>')
        return out

    nav_points = '\n'.join(points(toc, 2))
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">\n'
        f'<head><meta name="dtb:uid" content="{_html.escape(identifier)}"/></head>\n'
        f'<docTitle><text>{_html.escape(title)}</text></docTitle>\n'
        f'<navMap>\n{nav_points}\n</navMap>\n</ncx>\n'
    )


def make_opf(identifier: str, title: str, chapters, images) -> str:
    modified = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    manifest = [
        '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>',
        '<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>',
        '<item id="style" href="style/main.css" media-type="text/css"/>',
        # id matches the EPUB 2 <meta name="cover"> — CrossPoint's Tier 1 OPF cover lookup
        '<item id="cover-image" href="images/cover.png" media-type="image/png" properties="cover-image"/>',
        '<item id="cover" href="cover.xhtml" media-type="application/xhtml+xml"/>',
    ]
    manifest += [f'<item id="c{i}" href="{c.file_name}" media-type="application/xhtml+xml"/>'
                 for i, c in enumerate(chapters)]
    manifest += [f'<item id="img{i}" href="{href}" media-type="image/jpeg"/>'
                 for i, (href, _data) in enumerate(images)]
    # nav is excluded from the spine — CrossPoint locates it via properties="nav" in
    # the manifest and does not respect linear="no". Cover is the first spine item.
    spine = ['<itemref idref="cover"/>'] + [f'<itemref idref="c{i}"/>' for i in range(len(chapters))]
    nl = '\n    '
    return (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="book-id">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        f'    <dc:identifier id="book-id">{identifier}</dc:identifier>\n'
        f'    <dc:title>{_html.escape(title)}</dc:title>\n'
        '    <dc:language>en</dc:language>\n'
        '    <dc:creator>CrossPoint Reader Project</dc:creator>\n'
        f'    <meta property="dcterms:modified">{modified}</meta>\n'
        '    <meta name="cover" content="cover-image"/>\n'
        '  </metadata>\n'
        f'  <manifest>\n    {nl.join(manifest)}\n  </manifest>\n'
        f'  <spine toc="ncx">\n    {nl.join(spine)}\n  </spine>\n'
        '</package>\n'
    )


CONTAINER_XML = (
    '<?xml version="1.0" encoding="utf-8"?>\n'
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
    '  <rootfiles><rootfile full-path="OEBPS/content.opf" '
    'media-type="application/oebps-package+xml"/></rootfiles>\n'
    '</container>\n'
)


def build_epub(include_docs: bool, output: Path):
    if include_docs:
        title, subtitle, identifier = 'nexlibrOS Documentation', 'Documentation', 'nexlibros-docs-v1'
    else:
        title, subtitle, identifier = 'nexlibrOS User Guide', 'User Guide', 'nexlibros-user-guide-v1'

    guide = user_guide_chapters()
    groups = docs_groups() if include_docs else []
    chapters = guide + [c for _title, group in groups for c in group]

    for c in chapters:
        c.body = lhtml.fragment_fromstring(c.html, create_parent='body')

    # source markdown path -> (first chapter file, {heading id: chapter file})
    targets = {}
    for c in chapters:
        first_file, ids = targets.setdefault(c.source.resolve(), (c.file_name, {}))
        for el in c.body.iter():
            if isinstance(el.tag, str) and el.get('id'):
                ids.setdefault(el.get('id'), c.file_name)

    images = {}
    for c in chapters:
        c.source = c.source.resolve()
        rewrite_links(c, targets)
        collect_images(c, images)

    guide_entries = [TocEntry(c.title, c.file_name) for c in guide]
    toc = [TocEntry('Cover', 'cover.xhtml')]
    if groups:
        toc.append(TocEntry('User Guide', guide[0].file_name, guide_entries))
        toc += [TocEntry(t, group[0].file_name, [TocEntry(c.title, c.file_name) for c in group])
                for t, group in groups]
    else:
        toc += guide_entries

    cover_body = ('<body><div class="cover-page">'
                  f'<img src="images/cover.png" alt="{_html.escape(title)} cover"/>'
                  '</div></body>')
    image_list = list(images.values())

    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo('mimetype'), 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', CONTAINER_XML)
        z.writestr('OEBPS/content.opf', make_opf(identifier, title, chapters, image_list))
        z.writestr('OEBPS/toc.ncx', make_ncx(identifier, title, toc))
        z.writestr('OEBPS/nav.xhtml', make_nav(toc))
        z.writestr('OEBPS/style/main.css', CSS)
        z.writestr('OEBPS/images/cover.png', make_cover_png(subtitle))
        z.writestr('OEBPS/cover.xhtml', make_xhtml(title, cover_body))
        for c in chapters:
            body_xml = etree.tostring(c.body, encoding='unicode', method='xml')
            z.writestr(f'OEBPS/{c.file_name}', make_xhtml(c.title, body_xml))
        for href, data in image_list:
            z.writestr(f'OEBPS/{href}', data)

    print(f'Generated: {output} ({len(chapters)} chapters, {len(image_list)} images)')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--docs', action='store_true',
                        help='append docs/*.md and docs/contributing/*.md after the user guide')
    parser.add_argument('-o', '--output', type=Path, help='output .epub path')
    args = parser.parse_args()
    output = args.output or (OUTPUT_DOCS if args.docs else OUTPUT_GUIDE)
    build_epub(args.docs, output)


if __name__ == '__main__':
    main()
