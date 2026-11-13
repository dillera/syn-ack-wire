#!/usr/bin/env python3
"""The Palm client's fonts: pixel fonts drawn from the desktop client's OFL fonts (desktop/fonts), as pilrc font
files for the Palm's high-density screen, and a C table of their glyph widths for the PC preview.

Usage:
    palm_fonts.py OUTDIR     writes OUTDIR/glyphs<N>.bin (the font as rectangles, a 'Glyf' resource the game draws
                             itself: src/palm/text.c), OUTDIR/font<N>.txt (its pixels, readable) and OUTDIR/palm_fonts.h

Each font is one-bit, as the Palm draws it, and covers ASCII 32-126. The server's currency sign is a backslash on
the wire; here backslash is drawn as a yen sign, so the game's text needs no translation. A mono font gets one
advance for every glyph; the others keep their own. Rows that no glyph uses, above and below, are cut off, so the
line height is what the letters need.

The Palm's own font manager is not used: on the Clie TG50 it drew custom font families in its system font. Instead
each glyph is a few filled rectangles (rows of pixels merged downwards), which the game draws in any colour, at the
screen's own size, on the Palm and in the PC preview alike. glyphs<N>.bin: height, ascent, first character, count;
then a 16-bit big-endian offset for each glyph; at each, its width, its number of rectangles, and each rectangle as
x, y, w, h bytes.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
FONTS = ROOT / "desktop" / "fonts"

# id, name, file, size, weight (variable fonts), mono, extra spacing
SPECS = [
    (0, "TINY", "ShareTechMono-Regular.ttf", 11, None, True, 0),
    (1, "SMALL", "ShareTechMono-Regular.ttf", 13, None, True, 0),
    (2, "TEXT", "VT323-Regular.ttf", 20, None, True, 0),
    (3, "HEAD", "Orbitron.ttf", 12, b"Bold", False, 1),
    (4, "BIG", "Orbitron.ttf", 18, b"Black", False, 1),
    (5, "LOGO", "Orbitron.ttf", 30, b"Black", False, 2),
]
FIRST, LAST = 32, 126


def yen(draw, w, h, asc, ink):
    """A yen sign in a w x h cell: a Y with two bars, drawn in pixels so it matches any font."""
    top = max(1, asc - int(h * 0.62))
    mid = top + (asc - top) // 2
    cx = w // 2
    for y in range(top, mid + 1):
        d = int((mid - y) * (cx - 1) / max(1, mid - top))
        draw.point((cx - 1 - d if cx - 1 - d >= 0 else 0, y), fill=ink)
        draw.point((cx + d if cx + d < w else w - 1, y), fill=ink)
    draw.line((cx - (w > 6), mid, cx - (w > 6), asc - 1), fill=ink)
    if w > 6:
        draw.line((cx, mid, cx, asc - 1), fill=ink)
    for y in (mid + 1, mid + 3 if asc - mid > 4 else mid + 2):
        if y < asc:
            draw.line((1, y, w - 2, y), fill=ink)


def glyphs(spec):
    _, _, file, size, weight, mono, spacing = spec
    font = ImageFont.truetype(str(FONTS / file), size)
    if weight:
        font.set_variation_by_name(weight)
    asc, desc = font.getmetrics()
    h = asc + desc
    adv0 = round(font.getlength("0")) + spacing
    out = {}
    for c in range(FIRST, LAST + 1):
        ch = chr(c)
        w = adv0 if mono else max(1, round(font.getlength(ch)) + spacing)
        if ch == " " and not mono:
            w = max(3, round(font.getlength(" ")))
        img = Image.new("1", (w + 8, h), 0)
        d = ImageDraw.Draw(img)
        d.fontmode = "1"
        if ch == "\\":
            yen(d, w, h, asc, 1)
        else:
            d.text((0, 0), ch, font=font, fill=1)
        out[c] = [[img.getpixel((x, y)) for x in range(w)] for y in range(h)]
    # cut the rows no glyph uses
    used = [any(any(g[y]) for g in out.values()) for y in range(h)]
    top = used.index(True)
    bottom = h - used[::-1].index(True)
    top = max(0, top - 1)               # a row of air above the tallest letter
    bottom = min(h, bottom + 1)
    for c in out:
        rows = out[c][top:bottom]
        while len(rows) < bottom - top:
            rows.append([0] * len(rows[0]))
        out[c] = rows
    return out, asc - top, bottom - asc



def font_text(gl, asc, desc):
    lines = ["fontType 36864", f"ascent {asc}", f"descent {desc}"]
    for c in range(FIRST, LAST + 1):
        lines.append(f"GLYPH {c}")
        lines += ["".join("#" if p else "-" for p in row) for row in gl[c]]
    return "\n".join(lines) + "\n"


def rectangles(rows):
    """The glyph's pixels as rectangles: each run in a row, extended down over the rows that have the same run."""
    runs = []
    for row in rows:
        r, x = set(), 0
        while x < len(row):
            if row[x]:
                a = x
                while x < len(row) and row[x]:
                    x += 1
                r.add((a, x))
            else:
                x += 1
        runs.append(r)
    out = []
    for y, r in enumerate(runs):
        for (a, b) in sorted(r):
            h = 1
            while y + h < len(runs) and (a, b) in runs[y + h]:
                runs[y + h].discard((a, b))
                h += 1
            out.append((a, y, b - a, h))
    return out


def glyph_bin(gl, asc, desc):
    count = LAST - FIRST + 1
    head = bytearray([asc + desc, asc, FIRST, count])
    body = bytearray()
    offs = []
    for c in range(FIRST, LAST + 1):
        offs.append(len(head) + 2 * count + len(body))
        rects = rectangles(gl[c])
        body += bytes([len(gl[c][0]), len(rects)])
        for r in rects:
            body += bytes(r)
    for o in offs:
        head += bytes([o >> 8, o & 255])
    return bytes(head + body)


def write_font(spec, outdir):
    gl, asc, desc = glyphs(spec)
    (outdir / f"font{spec[0]}.txt").write_text(font_text(gl, asc, desc))
    (outdir / f"glyphs{spec[0]}.bin").write_bytes(glyph_bin(gl, asc, desc))
    widths = [len(gl[c][0]) for c in range(FIRST, LAST + 1)]
    return asc + desc, asc, widths


def main():
    outdir = Path(sys.argv[1])
    outdir.mkdir(parents=True, exist_ok=True)
    h = ["/* GENERATED by tools/palm_fonts.py -- do not edit. The Palm client's fonts (src/palm/synack.rcp loads",
         " * build/palm/font<N>.txt): their ids, line heights and ascents, and every glyph's width for the PC preview. */",
         "#ifndef PALM_FONTS_H", "#define PALM_FONTS_H", ""]
    widths_all = []
    for spec in SPECS:
        height, asc, widths = write_font(spec, outdir)
        n = spec[1]
        h += [f"#define F_{n} {spec[0]}", f"#define F_{n}_H {height}", f"#define F_{n}_ASC {asc}"]
        if spec[5]:
            h.append(f"#define F_{n}_W {widths[0]}")
        widths_all.append(widths)
    h += ["", f"#define PALM_FONTS {len(SPECS)}", "#ifdef PALM_FONT_WIDTHS",
          f"static const unsigned char palm_font_w[{len(SPECS)}][{LAST - FIRST + 1}] = {{"]
    for w in widths_all:
        h.append("    { " + ", ".join(str(x) for x in w) + " },")
    h += ["};", "#endif", "", "#endif"]
    (outdir / "palm_fonts.h").write_text("\n".join(h) + "\n")


if __name__ == "__main__":
    main()
