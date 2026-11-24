#!/usr/bin/env python3
"""gen_ytd.py [--mips N (default 0 = the full chain)] [--scale K] OUT.ytd NAME=IMAGE.png [NAME=IMAGE.png ...]

Writes an RDR2 texture dictionary (.ytd, RSC8 resource) that Lenny's Mod Loader streams as an add-on and DRAW_SPRITE
draws by (file name, texture name). No Windows tool needed. The layout copies the one of an add-on .ytd that works
in the game (PDO Reloaded's PDOR_ICONS.ytd, read 16.11; tested build 1.0.1491.50):

  header   "RSC8", version 0x01000002, virtual flags 0x00010000, physical flags 0x00040002, then raw deflate of
           the 0x10000-byte virtual (system) part followed by the 0x40000-byte physical (graphics) part
  virtual  at 0x50000000: pgDictionary { vtable 0, page map ptr, 0, refs 1, hashes {ptr,count,cap},
           textures {ptr,count,cap} }, the page map (0x48: 02 02), one 0x80-byte texture per entry, one 0x50-byte
           view block per entry, the texture pointer array, the hash array, the names; padding 0xCF
  texture  +0x08 u32 two bits 01 per mip (with 1x1); +0x0C u32 1; +0x10 02 80 00 18; +0x18 u16 width, u16 height, u16 depth 1,
           u8 1, u8 format (0x4D = BC3); +0x20 0D 00 u8 mips 00 00 00 01 00; +0x28 name ptr; +0x30 view ptr;
           +0x38 data ptr (0x60000000 + offset in the physical part); +0x78 CD padding
  entries  sorted by joaat(name), the hash array in the same order
  data     BC3 blocks, linear, the whole mip chain down to 1x1 (one mip draws as garbage in game)

Images must be RGBA with sides a multiple of 4; up to 0x10000 bytes of BC3 per 0x20000 page is plenty for HUD icons.
Bigger textures (the trader's paper, 24.11): the physical part is one block whose size is in the header - physical
flags = (size >> 16) << 16 | 2, read from add-on .ytd files of other mods on his PC (0x00ab0002 decompresses to
0x10000 + 0xab0000 bytes, a 1024x1024 texture starts at 0x60000000 and runs across the old 0x20000 pages). So when a
texture does not fit a page, the textures are laid out back to back (each at a 0x1000 boundary) and the block is
sized to fit; every mip is padded to whole 4x4 blocks (a 127-wide mip is stored as 32 blocks, as the GPU reads it).
Needs Pillow (to read the images).
"""
import struct
import sys
import zlib

from PIL import Image

VBASE, PBASE = 0x50000000, 0x60000000
VSIZE, PSIZE = 0x10000, 0x40000
PAGE = 0x20000  # data of one texture never crosses a physical page (as in the sample)


def joaat(s):
    h = 0
    for c in s.lower().encode():
        h = (h + c) & 0xFFFFFFFF
        h = (h + (h << 10)) & 0xFFFFFFFF
        h ^= h >> 6
    h = (h + (h << 3)) & 0xFFFFFFFF
    h ^= h >> 11
    return (h + (h << 15)) & 0xFFFFFFFF


def rgb565(c):
    return ((c[0] * 31 + 127) // 255) << 11 | ((c[1] * 63 + 127) // 255) << 5 | ((c[2] * 31 + 127) // 255)


def unpack565(v):
    r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
    return ((r * 255 + 15) // 31, (g * 255 + 31) // 63, (b * 255 + 15) // 31)


def bc3_block(px):
    """px: 16 RGBA tuples, row-major. Returns 16 bytes."""
    alphas = [p[3] for p in px]
    a0, a1 = max(alphas), min(alphas)
    if a0 == a1:
        alpha = bytes([a0, a1]) + bytes(6)
    else:
        pal = [a0, a1] + [((7 - i) * a0 + i * a1 + 3) // 7 for i in range(1, 7)]
        bits = 0
        for k, a in enumerate(alphas):
            idx = min(range(8), key=lambda i: abs(pal[i] - a))
            bits |= idx << (3 * k)
        alpha = bytes([a0, a1]) + bits.to_bytes(6, 'little')
    seen = [p for p in px if p[3] > 0] or px
    lum = lambda p: p[0] * 299 + p[1] * 587 + p[2] * 114
    hi, lo = max(seen, key=lum), min(seen, key=lum)
    c0, c1 = rgb565(hi), rgb565(lo)
    if c0 < c1:
        c0, c1 = c1, c0
    if c0 == c1:
        return alpha + struct.pack('<HHI', c0, c1, 0)
    e0, e1 = unpack565(c0), unpack565(c1)
    pal = [e0, e1, tuple((2 * a + b + 1) // 3 for a, b in zip(e0, e1)), tuple((a + 2 * b + 1) // 3 for a, b in zip(e0, e1))]
    bits = 0
    for k, p in enumerate(px):
        idx = min(range(4), key=lambda i: sum((pal[i][j] - p[j]) ** 2 for j in range(3)))
        bits |= idx << (2 * k)
    return alpha + struct.pack('<HHI', c0, c1, bits)


def bc3(im):
    w, h = im.size
    p = im.load()
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            out += bc3_block([p[bx + x, by + y] for y in range(4) for x in range(4)])
    return bytes(out)


def mip_chain(im, levels):
    """BC3 of `levels` mips from im down (1 = just im; 0 = the whole chain to 1x1), and the level count."""
    w, h = im.size
    if levels == 0:
        levels = max(w, h).bit_length()
    out = bytearray()
    for lv in range(levels):
        mw, mh = max(1, w >> lv), max(1, h >> lv)
        m = im if lv == 0 else im.resize((mw, mh), Image.LANCZOS)
        pw, ph = (mw + 3) // 4 * 4, (mh + 3) // 4 * 4
        if (pw, ph) != (mw, mh):  # a block is 4x4: the rest of it is never sampled
            pad = Image.new('RGBA', (pw, ph), (0, 0, 0, 0))
            pad.paste(m, (0, 0))
            m = pad
        out += bc3(m)
    return bytes(out), levels


def build(entries, levels=0):
    """entries: list of (name, RGBA image); levels: mips per texture (0 = the full chain - needed: checked in game
    16.11, one-mip textures draw as red garbage while the full chain at 64 and at 128 draws right). Returns bytes."""
    entries = sorted(entries, key=lambda e: joaat(e[0]))
    n = len(entries)
    chains = [mip_chain(im, levels) for _, im in entries]
    big = any(len(data) > PAGE for data, _ in chains)
    if big:  # one block, textures back to back at 0x1000 boundaries
        need = 0
        for data, _ in chains:
            need = (need + 0xFFF) & ~0xFFF
            need += len(data)
        psize = max(PSIZE, (need + 0xFFFF) & ~0xFFFF)
    else:
        psize = PSIZE
    v = bytearray(b'\xcf' * VSIZE)
    phys = bytearray(b'\xcf' * psize)
    TEX, VIEW = 0xC60, 0xC60 + n * 0x80
    PTRS = VIEW + n * 0x50
    HASHES = PTRS + n * 8
    NAMES = (HASHES + n * 4 + 15) & ~15
    assert NAMES + n * 0x40 < VSIZE, 'too many textures for one virtual page'
    # dictionary + page map
    v[0:0x40] = struct.pack('<QQQQQHHIQHHI', 0, VBASE + 0x40, 0, 1, VBASE + HASHES, n, n, 0, VBASE + PTRS, n, n, 0)
    v[0x40:TEX] = bytes(TEX - 0x40)
    v[0x48:0x4A] = b'\x02\x02'
    v[TEX - 8:TEX] = b'\xcd' * 8
    off = 0
    name_at = NAMES
    for i, (name, im) in enumerate(entries):
        w, h = im.size
        assert w % 4 == 0 and h % 4 == 0, name
        data, nmips = chains[i]
        if big:
            off = (off + 0xFFF) & ~0xFFF
        elif off // PAGE != (off + len(data) - 1) // PAGE:
            off = (off // PAGE + 1) * PAGE
        assert off + len(data) <= psize, 'physical part full'
        phys[off:off + len(data)] = data
        t = bytearray(0x80)
        struct.pack_into('<QII', t, 0, 0, sum(1 << (2 * k) for k in range(nmips)), 1)  # vtable, 01 per mip, 1
        t[0x10:0x18] = bytes.fromhex('0280001800000000')
        struct.pack_into('<HHHBB', t, 0x18, w, h, 1, 1, 0x4D)      # BC3
        t[0x20:0x28] = bytes([0x0D, 0x00, nmips, 0, 0, 0, 1, 0])    # mips (with 1x1)
        struct.pack_into('<QQQ', t, 0x28, VBASE + name_at, VBASE + VIEW + i * 0x50, PBASE + off)
        t[0x78:0x80] = b'\xcd' * 8
        v[TEX + i * 0x80:TEX + (i + 1) * 0x80] = t
        view = bytearray(0x50)
        view[0x10:0x1C] = bytes.fromhex('010400000000000005000000')
        v[VIEW + i * 0x50:VIEW + (i + 1) * 0x50] = view
        struct.pack_into('<Q', v, PTRS + i * 8, VBASE + TEX + i * 0x80)
        struct.pack_into('<I', v, HASHES + i * 4, joaat(name))
        raw = name.lower().encode() + b'\0'
        slot = (len(raw) + 15) & ~15
        v[name_at:name_at + slot] = raw + b'\xcd' * (slot - len(raw))
        name_at += slot
        off += len(data)
    v[HASHES + n * 4:NAMES] = b'\xcd' * (NAMES - HASHES - n * 4)
    body = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = body.compress(bytes(v) + bytes(phys)) + body.flush()
    return struct.pack('<4sIII', b'RSC8', 0x01000002, 0x00010000, (psize >> 16) << 16 | 2) + packed


def main():
    argv = sys.argv[1:]
    levels = 0
    if '--mips' in argv:
        i = argv.index('--mips'); levels = int(argv[i + 1]); del argv[i:i + 2]
    scale = 1
    if '--scale' in argv:
        i = argv.index('--scale'); scale = int(argv[i + 1]); del argv[i:i + 2]
    if len(argv) < 2:
        sys.exit(__doc__)
    sys.argv[1:] = argv
    entries = []
    for arg in sys.argv[2:]:
        name, path = arg.split('=', 1)
        im = Image.open(path).convert('RGBA')
        if scale != 1:
            im = im.resize((im.size[0] * scale, im.size[1] * scale), Image.BICUBIC)
        entries.append((name, im))
    data = build(entries, levels)
    with open(sys.argv[1], 'wb') as f:
        f.write(data)
    print(f'{sys.argv[1]}: {len(entries)} textures, {len(data)} bytes')


if __name__ == '__main__':
    main()
