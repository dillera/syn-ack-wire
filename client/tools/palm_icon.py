#!/usr/bin/env python3
"""The Palm client's launcher icons, as BMPs for pilrc (src/palm/synack.rcp): a circuit chip in the syndicates'
colours. Writes OUTDIR/icon1.bmp (1-bit, 32x32), icon16.bmp (32x32) and icon16hd.bmp (64x64, high density), and
the small icons, sicon1.bmp (15x9), sicon16.bmp and sicon16hd.bmp. Magenta is transparent."""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

KEY = (255, 0, 255)
SYN = [(0x44, 0xD4, 0xE8), (0xE0, 0x97, 0x4F), (0xB4, 0x6C, 0xFF), (0x9C, 0xD4, 0x3C), (0xFF, 0x45, 0x60),
       (0x2E, 0xE5, 0x9A), (0xFF, 0xD9, 0x3A)]


def chip(size, colour=True):
    s = size / 32
    img = Image.new("RGB", (size, size), KEY)
    d = ImageDraw.Draw(img)
    ink = (0, 0, 0)
    box = [round(6 * s), round(5 * s), round(26 * s) - 1, round(23 * s) - 1]
    for i in range(5):                  # pins, each a syndicate's colour
        x = box[0] + round((2 + i * 4) * s)
        pin = SYN[i] if colour else ink
        d.rectangle([x, box[1] - round(3 * s), x + max(0, round(s) - 1), box[1] - 1], fill=pin)
        d.rectangle([x, box[3] + 1, x + max(0, round(s) - 1), box[3] + round(3 * s)], fill=SYN[i + 2] if colour else ink)
    d.rectangle(box, fill=(0x13, 0x23, 0x3A) if colour else ink, outline=(0x33, 0xDD, 0xFF) if colour else ink)
    if colour:
        inner = [box[0] + round(3 * s), box[1] + round(3 * s), box[2] - round(3 * s), box[3] - round(3 * s)]
        d.rectangle(inner, outline=(0xFF, 0x33, 0xCC))
        cx, cy = (box[0] + box[2]) // 2, (box[1] + box[3]) // 2
        d.rectangle([cx - round(2 * s), cy - round(2 * s), cx + round(2 * s), cy + round(2 * s)], fill=(0xFF, 0xFF, 0xFF))
    else:
        d.rectangle([box[0] + 2, box[1] + 2, box[2] - 2, box[3] - 2], fill=(255, 255, 255))
        cx, cy = (box[0] + box[2]) // 2, (box[1] + box[3]) // 2
        d.rectangle([cx - 2, cy - 2, cx + 2, cy + 2], fill=ink)
    return img


def small(w, h, colour=True):
    img = Image.new("RGB", (w, h), KEY)
    d = ImageDraw.Draw(img)
    s = w / 15
    d.rectangle([round(3 * s), round(1 * s), round(12 * s) - 1, h - 1 - round(1 * s)],
                fill=(0x13, 0x23, 0x3A) if colour else (0, 0, 0), outline=(0x33, 0xDD, 0xFF) if colour else (0, 0, 0))
    cx, cy = w // 2, h // 2
    d.rectangle([cx - round(s), cy - round(s), cx + round(s), cy + round(s)], fill=(255, 255, 255))
    return img


def one_bit(img, path):
    bw = Image.new("1", img.size, 1)
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if img.getpixel((x, y)) == (0, 0, 0):
                bw.putpixel((x, y), 0)
    bw.save(path)


def main():
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    one_bit(chip(32, False), out / "icon1.bmp")
    chip(32).save(out / "icon16.bmp")
    chip(64).save(out / "icon16hd.bmp")
    one_bit(small(15, 9, False), out / "sicon1.bmp")
    small(15, 9).save(out / "sicon16.bmp")
    small(30, 18).save(out / "sicon16hd.bmp")


if __name__ == "__main__":
    main()
