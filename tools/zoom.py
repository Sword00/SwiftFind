"""Zoom a 24/32bpp BMP by an integer factor (nearest neighbour) into a PNG.

Used to inspect 16px tray icons that are too small to judge by eye.
usage: python zoom.py in.bmp out.png factor [x y w h]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_studio import Img

def read_bmp(path):
    d = open(path, 'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    topdown = h < 0
    h = abs(h)
    stride = ((w * bpp // 8 + 3) // 4) * 4
    img = Img(w, h)
    for y in range(h):
        sy = y if topdown else (h - 1 - y)
        row = off + sy * stride
        for x in range(w):
            p = row + x * (bpp // 8)
            b, g, r = d[p], d[p + 1], d[p + 2]
            i = y * w + x
            img.r[i] = r / 255.0
            img.g[i] = g / 255.0
            img.b[i] = b / 255.0
            img.a[i] = 1.0
    return img

def crop(img, x, y, w, h):
    out = Img(w, h)
    for yy in range(h):
        for xx in range(w):
            sx, sy = x + xx, y + yy
            if 0 <= sx < img.w and 0 <= sy < img.h:
                si = sy * img.w + sx
                di = yy * w + xx
                out.r[di] = img.r[si]; out.g[di] = img.g[si]
                out.b[di] = img.b[si]; out.a[di] = 1.0
    return out

def zoom(img, f):
    out = Img(img.w * f, img.h * f)
    for y in range(out.h):
        for x in range(out.w):
            si = (y // f) * img.w + (x // f)
            di = y * out.w + x
            out.r[di] = img.r[si]; out.g[di] = img.g[si]
            out.b[di] = img.b[si]; out.a[di] = 1.0
    return out

if __name__ == '__main__':
    src = read_bmp(sys.argv[1])
    f = int(sys.argv[3])
    if len(sys.argv) >= 7:
        src = crop(src, int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6]),
                   int(sys.argv[7]) if len(sys.argv) > 7 else int(sys.argv[6]))
    zoom(src, f).save(sys.argv[2])
    print('wrote', sys.argv[2], '%dx%d' % (src.w * f, src.h * f))
