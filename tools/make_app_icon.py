"""Build src/app.ico from the chosen icon variant.

Multi-resolution Windows ICO: 256px as PNG (small file), the rest as 32bpp
BMP entries. Sizes cover SM_CXSMICON at 100/125/150/200/250% DPI plus the
standard Explorer sizes.
"""
import os, sys, struct, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_studio as S

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src', 'app.ico')
SIZES = [256, 128, 64, 48, 40, 32, 24, 20, 16]

def bmp_entry(img):
    w = h = img.w
    hdr = struct.pack('<IiiHHIIiiII', 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    px = bytearray()
    for y in range(h - 1, -1, -1):          # bottom-up
        for x in range(w):
            i = y * w + x
            a = img.a[i]
            if a <= 0.0:
                px += b'\x00\x00\x00\x00'
            else:
                px += bytes((
                    min(255, int(img.b[i] / a * 255 + 0.5)),
                    min(255, int(img.g[i] / a * 255 + 0.5)),
                    min(255, int(img.r[i] / a * 255 + 0.5)),
                    min(255, int(a * 255 + 0.5)),
                ))
    stride = ((w + 31) // 32) * 4            # 1bpp rows padded to 4 bytes
    mask = bytearray(stride * h)             # all zero = opaque
    return hdr + bytes(px) + bytes(mask)

def main():
    icon = S.v3_bolt()
    icon_small = S.v3_bolt_small()
    entries, blobs = [], []
    offset = 6 + 16 * len(SIZES)
    for size in SIZES:
        src = icon_small if size <= 24 else icon
        img = src if size == 256 else S.downscale(src, size)
        # PNG for every entry. Vista+ reads PNG-compressed icons at any size and
        # this app already requires Win10, so the BMP fallback buys nothing and
        # costs ~18 KB of uncompressed 32bpp pixels.
        data = img.to_png()
        entries.append((size, len(data), offset))
        blobs.append(data)
        offset += len(data)

    out = bytearray(struct.pack('<HHH', 0, 1, len(SIZES)))
    for size, n, off in entries:
        out += struct.pack('<BBBBHHII', 0 if size >= 256 else size,
                           0 if size >= 256 else size, 0, 0, 1, 32, n, off)
    for b in blobs:
        out += b
    with open(OUT, 'wb') as f:
        f.write(out)
    print('wrote %s  %d bytes  sizes=%s' % (OUT, len(out), SIZES))

if __name__ == '__main__':
    main()
