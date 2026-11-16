#!/usr/bin/env python3
"""gen_paw_core.py HEARTS_DIR OUT_DIR [--wear 0..3]

The dog's core icon (D148): our own paw, drawn in the style of the game's core icons, in 16 drained states like
rpg_core_health core_state_0..15. The paw is drawn from scratch (four oval toes, a three-lobed main pad) at 1024 px and
boxed down to 64x64 like the game's icons; its white, its drained grey and the speckled band at the drain line are taken
from the game's own hearts (HEARTS_DIR: rpg_core_health core_state_0..15.png, e.g. from the femga/rdr3_discoveries
texture dump, "without bg"), the drained height of each state is measured on the heart of the same state; the wear
(the operator wanted it «потёртая», like the game's paw blip) is speckle noise with a fixed seed, level 3 by default.
Output: OUT_DIR/paw_core_0..15.png, packed into frontier_dog_hud.ytd by gen_ytd.py and streamed by LML.
Needs Pillow.
"""
import sys
from PIL import Image, ImageDraw, ImageFilter
import math, random
argv = sys.argv[1:]
WEAR = 3
if '--wear' in argv:
    i = argv.index('--wear'); WEAR = int(argv[i + 1]); del argv[i:i + 2]
if len(argv) != 2: sys.exit(__doc__)
HEARTS, OUT = argv
S = 1024
def ell(d, cx, cy, rx, ry, rot):
    pts = []
    for i in range(96):
        t = 2 * math.pi * i / 96
        x, y = rx * math.cos(t), ry * math.sin(t)
        c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
        pts.append(((cx + x * c - y * s_) * S, (cy + x * s_ + y * c) * S))
    d.polygon(pts, fill=255)
m = Image.new('L', (S, S), 0); d = ImageDraw.Draw(m)
# units: fraction of S, centre .5,.5; the paw spans ~.38..62 wide
O = .5
# main pad: a rounded triangle, narrow on top, three soft lobes at the bottom (Catmull-Rom through these points)
PAD = [(0, -.07), (.075, -.035), (.13, .03), (.155, .09), (.135, .145), (.085, .16), (.045, .15), (0, .175),
       (-.045, .15), (-.085, .16), (-.135, .145), (-.155, .09), (-.13, .03), (-.075, -.035)]
def catmull(points, steps=24):
    out = []; n = len(points)
    for i in range(n):
        p0, p1, p2, p3 = points[i - 1], points[i], points[(i + 1) % n], points[(i + 2) % n]
        for k in range(steps):
            t = k / steps; t2, t3 = t * t, t * t * t
            out.append(tuple(.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                   + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in (0, 1)))
    return out
d.polygon([((O + x) * S, (O + .07 + y) * S) for x, y in catmull(PAD)], fill=255)
# toes
ell(d, O - .062, O - .125, .05, .068, -6)
ell(d, O + .062, O - .125, .05, .068, 6)
ell(d, O - .165, O - .035, .046, .062, -26)
ell(d, O + .165, O - .035, .046, .062, 26)
m = m.filter(ImageFilter.GaussianBlur(6)).point(lambda v: 255 if v > 127 else 0)
bb = m.getbbox()
shape = m.crop(bb)
TARGET_H = 28.0   # px in the 64 frame, like the heart (27) and the earlier paw (28)
k = TARGET_H * 16 / shape.size[1]
shape = shape.resize((round(shape.size[0] * k), round(shape.size[1] * k)), Image.LANCZOS).point(lambda v: 255 if v > 127 else 0)
frame = Image.new('L', (1024, 1024), 0)
frame.paste(shape, (round(31.5 * 16 - shape.size[0] / 2), round(32.0 * 16 - shape.size[1] / 2)))
alpha = frame.resize((64, 64), Image.BOX)
pb = alpha.point(lambda v: 255 if v > 40 else 0).getbbox()

hearts = [Image.open(f'{HEARTS}/core_state_{n}.png').convert('RGBA') for n in range(16)]
hb = hearts[15].getchannel('A').point(lambda v: 255 if v > 128 else 0).getbbox()
def lum(c): return (c[0] * 299 + c[1] * 587 + c[2] * 114) // 1000
def line_of(im):
    px = im.load(); rows = []
    for x in range(hb[0], hb[2]):
        col = [y for y in range(hb[1], hb[3]) if px[x, y][3] >= 128]
        if len(col) < 15: continue
        rows.append(col[0] + sum(1 for y in col if lum(px[x, y]) < 200))
    return sum(rows) / len(rows)
fills = [max(0.0, min(1.0, (hb[3] - line_of(h)) / (hb[3] - hb[1]))) for h in hearts]
for n in range(1, 16): fills[n] = max(fills[n], fills[n - 1])
BAND = 5
src = min(range(1, 15), key=lambda n: abs(line_of(hearts[n]) - (hb[1] + (hb[3] - hb[1]) * .38)))
Ls = round(line_of(hearts[src])); sp = hearts[src].load()
xs = [x for x in range(hb[0], hb[2]) if all(sp[x, y][3] >= 128 for y in range(Ls - BAND, Ls + BAND + 1))]
pat = [[sp[x, y] for y in range(Ls - BAND, Ls + BAND + 1)] for x in xs]
white = (250, 250, 249); grey = (141, 142, 141)

# wear field: fine specks + coarse blotches, fixed seed
random.seed(11)
def noise(sigma):
    im = Image.new('L', (64, 64))
    im.putdata([random.randint(0, 255) for _ in range(64 * 64)])
    im = im.filter(ImageFilter.GaussianBlur(sigma))
    lo, hi = im.getextrema()
    return [(v - lo) / max(1, hi - lo) for v in im.getdata()]
fine, coarse = noise(.55), noise(1.8)
field = [.6 * a + .4 * b for a, b in zip(fine, coarse)]
inner = alpha.point(lambda v: 255 if v > 200 else 0).filter(ImageFilter.MinFilter(3))
LEVELS = {0: None, 1: (.56, .9), 2: (.5, .95), 3: (.44, 1.0)}   # threshold, strength
for lvl, prm in [(WEAR, LEVELS[WEAR])]:
    for n in range(16):
        out = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
        line = pb[1] + (1 - fills[n]) * (pb[3] - pb[1])
        for y in range(64):
            for x in range(64):
                a = alpha.getpixel((x, y))
                if not a: continue
                dy = round(y + .5 - line)
                if n == 15 or dy > BAND: c = white
                elif n == 0 or dy < -BAND: c = grey
                else: c = pat[(x - pb[0]) % len(pat)][dy + BAND]
                if prm:
                    t, s = prm
                    w = max(0.0, (field[y * 64 + x] - t) / (1 - t))
                    w = min(1.0, w * 1.6) * (1.0 if inner.getpixel((x, y)) else .45)
                    a = int(a * (1 - s * w))
                out.putpixel((x, y), (c[0], c[1], c[2], a))
        out.save(f'{OUT}/paw_core_{n}.png')
print("16 states written to", OUT)
