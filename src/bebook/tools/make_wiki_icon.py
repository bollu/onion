#!/usr/bin/env python3
# Draws bewiki's 74x74 app icon: a globe over an open book, in the Onion icon palette.
import math
import os
from PIL import Image, ImageDraw

S = 74
K = 8
W = S * K

OUTLINE = (0x40, 0x39, 0x6E, 255)
PAPER = (0xF2, 0xF2, 0xF2, 255)
PAGE_D = (0xD9, 0xEE, 0xFF, 255)
COVER = (0x70, 0x7C, 0xC0, 255)
SEA = (0x70, 0xBF, 0xFF, 255)
LAND = (0x96, 0xC3, 0x62, 255)

im = Image.new("RGBA", (W, W), (0, 0, 0, 0))
d = ImageDraw.Draw(im)

def sc(*v):
    return [x * K for x in v]

def poly(pts, fill):
    d.polygon([(x * K, y * K) for x, y in pts], fill=fill, outline=OUTLINE, width=LW)

LW = int(2.4 * K)

# --- open book, lower third ---
CY = 52
poly([(6, CY - 3), (35, CY - 8), (35, CY + 9), (6, CY + 6)], COVER)
poly([(68, CY - 3), (39, CY - 8), (39, CY + 9), (68, CY + 6)], COVER)
poly([(9, CY - 5), (35, CY - 10), (35, CY + 5), (9, CY + 2)], PAPER)
poly([(65, CY - 5), (39, CY - 10), (39, CY + 5), (65, CY + 2)], PAGE_D)
d.line(sc(37, CY - 11, 37, CY + 7), fill=OUTLINE, width=LW)

# --- globe, upper two thirds ---
GX, GY, R = 37, 28, 20
d.ellipse(sc(GX - R, GY - R, GX + R, GY + R), fill=SEA, outline=OUTLINE, width=LW)

# Two blobs of "land", clipped to the globe.
land = Image.new("RGBA", (W, W), (0, 0, 0, 0))
lg = ImageDraw.Draw(land)
lg.ellipse(sc(GX - 15, GY - 14, GX - 1, GY - 3), fill=LAND)
lg.ellipse(sc(GX - 6, GY + 1, GX + 13, GY + 12), fill=LAND)
lg.ellipse(sc(GX + 5, GY - 12, GX + 16, GY - 5), fill=LAND)
mask = Image.new("L", (W, W), 0)
ImageDraw.Draw(mask).ellipse(sc(GX - R + 1.4, GY - R + 1.4, GX + R - 1.4, GY + R - 1.4), fill=255)
im.paste(land, (0, 0), Image.composite(land.split()[3], Image.new("L", (W, W), 0), mask))

# Meridian + equator over the top.
grid = Image.new("RGBA", (W, W), (0, 0, 0, 0))
gd = ImageDraw.Draw(grid)
gd.ellipse(sc(GX - R * 0.42, GY - R, GX + R * 0.42, GY + R), outline=OUTLINE, width=int(1.5 * K))
for dy in (0, -10, 10):
    half = math.sqrt(max(R * R - dy * dy, 0))
    gd.line(sc(GX - half, GY + dy, GX + half, GY + dy), fill=OUTLINE, width=int(1.5 * K))
im.paste(grid, (0, 0), Image.composite(grid.split()[3], Image.new("L", (W, W), 0), mask))

# Redraw the globe rim so the grid does not bleed over it.
d.ellipse(sc(GX - R, GY - R, GX + R, GY + R), outline=OUTLINE, width=LW)

out = im.resize((S, S), Image.LANCZOS)
dest = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                    "../../../static/build/Icons/Default/app/wiki.png")
out.save(os.path.normpath(dest))
print("wrote", os.path.normpath(dest))
