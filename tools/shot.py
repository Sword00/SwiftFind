"""Grab a screen region into a 32bpp BMP (no PIL needed).

usage: python shot.py out.bmp [x y w h]
"""
import sys, ctypes
from ctypes import wintypes

user32, gdi32 = ctypes.windll.user32, ctypes.windll.gdi32
SRCCOPY = 0x00CC0020
DIB_RGB_COLORS = 0


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG),
                ('biHeight', wintypes.LONG), ('biPlanes', wintypes.WORD),
                ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', wintypes.LONG),
                ('biYPelsPerMeter', wintypes.LONG), ('biClrUsed', wintypes.DWORD),
                ('biClrImportant', wintypes.DWORD)]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [('bmiHeader', BITMAPINFOHEADER), ('bmiColors', wintypes.DWORD * 3)]


def grab(x, y, w, h):
    hdc_screen = user32.GetDC(0)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbmp = gdi32.CreateCompatibleBitmap(hdc_screen, w, h)
    gdi32.SelectObject(hdc_mem, hbmp)
    gdi32.BitBlt(hdc_mem, 0, 0, w, h, hdc_screen, x, y, SRCCOPY)

    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h          # top-down
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(hdc_mem, hbmp, 0, h, buf, ctypes.byref(bi), DIB_RGB_COLORS)

    gdi32.DeleteObject(hbmp)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(0, hdc_screen)
    return w, h, buf.raw


def write_bmp(path, w, h, rgba):
    import struct
    stride = w * 4
    hdr = b'BM' + struct.pack('<IHHI', 14 + 40 + stride * h, 0, 0, 14 + 40)
    ih = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, stride * h, 2835, 2835, 0, 0)
    # GetDIBits already hands back BGRA, which is exactly the BMP byte order.
    out = bytearray(rgba)
    out[3::4] = b'\xff' * (w * h)
    open(path, 'wb').write(hdr + ih + bytes(out))


if __name__ == '__main__':
    x, y, w, h = (0, 0) + tuple(user32.GetSystemMetrics(i) for i in (0, 1))
    if len(sys.argv) >= 6:
        x, y, w, h = (int(v) for v in sys.argv[2:6])
    W, H, px = grab(x, y, w, h)
    write_bmp(sys.argv[1], W, H, px)
    print('wrote %s  %dx%d at %d,%d' % (sys.argv[1], W, H, x, y))
