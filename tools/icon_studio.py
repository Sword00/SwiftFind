"""SwiftFind icon studio - procedural icon variants + contact sheet.

Pure stdlib. Shapes are signed-distance fields evaluated once per pixel, which
gives clean analytic anti-aliasing without supersampling.

Design brief for this app: double-Ctrl opens a floating rounded search field.
So the glyph family is built around a *field* (rounded square) or a lens that
carries keyboard semantics, not around a generic magnifying glass.
"""
import math, zlib, struct, os

OUT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- colour
def hexc(s, a=1.0):
    s = s.lstrip('#')
    return (int(s[0:2], 16) / 255.0, int(s[2:4], 16) / 255.0, int(s[4:6], 16) / 255.0, a)

def mix(c0, c1, t):
    t = max(0.0, min(1.0, t))
    return tuple(c0[k] + (c1[k] - c0[k]) * t for k in range(4))

def grad(c0, c1, p0, p1):
    dx, dy = p1[0] - p0[0], p1[1] - p0[1]
    L2 = dx * dx + dy * dy or 1.0
    def f(x, y):
        t = ((x - p0[0]) * dx + (y - p0[1]) * dy) / L2
        return mix(c0, c1, t)
    return f

# ---------------------------------------------------------------- sdf
def sd_round_box(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r

def sd_circle(px, py, cx, cy, r):
    return math.hypot(px - cx, py - cy) - r

def sd_ring(px, py, cx, cy, r, w):
    return abs(math.hypot(px - cx, py - cy) - r) - w * 0.5

def sd_segment(px, py, ax, ay, bx, by, w):
    vx, vy = bx - ax, by - ay
    wx, wy = px - ax, py - ay
    L2 = vx * vx + vy * vy or 1.0
    t = max(0.0, min(1.0, (wx * vx + wy * vy) / L2))
    return math.hypot(px - (ax + t * vx), py - (ay + t * vy)) - w * 0.5

def sd_polygon(px, py, pts):
    n = len(pts)
    d = (px - pts[0][0]) ** 2 + (py - pts[0][1]) ** 2
    s = 1.0
    j = n - 1
    for i in range(n):
        ex, ey = pts[j][0] - pts[i][0], pts[j][1] - pts[i][1]
        wx, wy = px - pts[i][0], py - pts[i][1]
        t = max(0.0, min(1.0, (wx * ex + wy * ey) / (ex * ex + ey * ey or 1.0)))
        bx, by = wx - ex * t, wy - ey * t
        d = min(d, bx * bx + by * by)
        c1 = py >= pts[i][1]
        c2 = py < pts[j][1]
        c3 = ex * wy > ey * wx
        if (c1 and c2 and c3) or ((not c1) and (not c2) and (not c3)):
            s = -s
        j = i
    return s * math.sqrt(d)

def sd_half(px, py, nx, ny, d):
    """Half-plane: positive on the (nx,ny) side."""
    return px * nx + py * ny - d

def sub(a, b):
    """a minus b (a real sdf intersection with the complement of b)."""
    return max(a, -b)

# ---------------------------------------------------------------- image
class Img:
    """Premultiplied RGBA, float 0..1."""
    def __init__(self, w, h):
        self.w, self.h = w, h
        n = w * h
        self.r = [0.0] * n
        self.g = [0.0] * n
        self.b = [0.0] * n
        self.a = [0.0] * n

    def blend(self, x, y, col, cov):
        if cov <= 0.0:
            return
        sa = cov * (col[3] if len(col) > 3 else 1.0)
        if sa <= 0.0:
            return
        i = y * self.w + x
        k = 1.0 - sa
        self.r[i] = col[0] * sa + self.r[i] * k
        self.g[i] = col[1] * sa + self.g[i] * k
        self.b[i] = col[2] * sa + self.b[i] * k
        self.a[i] = sa + self.a[i] * k

    def fill(self, sdf, color, bbox=None):
        if bbox:
            x0, y0, x1, y1 = bbox
        else:
            x0, y0, x1, y1 = 0, 0, self.w, self.h
        x0 = max(0, int(x0)); y0 = max(0, int(y0))
        x1 = min(self.w, int(x1) + 1); y1 = min(self.h, int(y1) + 1)
        cb = callable(color)
        for y in range(y0, y1):
            fy = y + 0.5
            for x in range(x0, x1):
                cov = 0.5 - sdf(x + 0.5, fy)
                if cov <= 0.0:
                    continue
                if cov > 1.0:
                    cov = 1.0
                self.blend(x, y, color(x + 0.5, fy) if cb else color, cov)

    def blit(self, src, ox, oy):
        for y in range(src.h):
            ty = oy + y
            if ty < 0 or ty >= self.h:
                continue
            for x in range(src.w):
                tx = ox + x
                if tx < 0 or tx >= self.w:
                    continue
                si = y * src.w + x
                sa = src.a[si]
                if sa <= 0.0:
                    continue
                di = ty * self.w + tx
                k = 1.0 - sa
                self.r[di] = src.r[si] + self.r[di] * k
                self.g[di] = src.g[si] + self.g[di] * k
                self.b[di] = src.b[si] + self.b[di] * k
                self.a[di] = sa + self.a[di] * k

    def is_transparent(self):
        return self.a[0] < 0.05 and self.a[self.w - 1] < 0.05

    def to_png(self):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            for x in range(self.w):
                i = y * self.w + x
                a = self.a[i]
                if a <= 0.0:
                    raw += b"\x00\x00\x00\x00"
                else:
                    raw += bytes((
                        min(255, int(self.r[i] / a * 255 + 0.5)),
                        min(255, int(self.g[i] / a * 255 + 0.5)),
                        min(255, int(self.b[i] / a * 255 + 0.5)),
                        min(255, int(a * 255 + 0.5)),
                    ))
        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data +
                    struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))
        ihdr = struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))

    def save(self, path):
        with open(path, "wb") as f:
            f.write(self.to_png())

def downscale(img, size):
    """Box filter, area average on premultiplied values."""
    out = Img(size, size)
    sx = img.w / size
    sy = img.h / size
    for y in range(size):
        y0, y1 = int(y * sy), int((y + 1) * sy)
        for x in range(size):
            x0, x1 = int(x * sx), int((x + 1) * sx)
            n = 0
            rr = gg = bb = aa = 0.0
            for yy in range(y0, max(y0 + 1, y1)):
                for xx in range(x0, max(x0 + 1, x1)):
                    i = yy * img.w + xx
                    rr += img.r[i]; gg += img.g[i]; bb += img.b[i]; aa += img.a[i]
                    n += 1
            if n:
                i = y * size + x
                out.r[i] = rr / n; out.g[i] = gg / n
                out.b[i] = bb / n; out.a[i] = aa / n
    return out

def upscale_nn(img, size):
    out = Img(size, size)
    for y in range(size):
        sy = y * img.h // size
        for x in range(size):
            sx = x * img.w // size
            si = sy * img.w + sx
            di = y * size + x
            out.r[di] = img.r[si]; out.g[di] = img.g[si]
            out.b[di] = img.b[si]; out.a[di] = img.a[si]
    return out

# ---------------------------------------------------------------- shapes
def bolt_points(cx, cy, size, angle_deg):
    """Classic lightning bolt, centred, rotated."""
    base = [(0.52, 0.00), (0.10, 0.58), (0.42, 0.58),
            (0.30, 1.00), (0.78, 0.40), (0.46, 0.40)]
    a = math.radians(angle_deg)
    ca, sa = math.cos(a), math.sin(a)
    pts = []
    for bx, by in base:
        lx = (bx - 0.5) * size
        ly = (by - 0.5) * size
        pts.append((cx + lx * ca - ly * sa, cy + lx * sa + ly * ca))
    return pts

# ---------------------------------------------------------------- variants
N = 256

BLUE0, BLUE1 = hexc('#4C7DFF'), hexc('#1A33B4')
TILE_BLUE = grad(BLUE0, BLUE1, (0, 0), (N, N))
INK0, INK1 = hexc('#262F42'), hexc('#0B0F15')
TILE_INK = grad(INK0, INK1, (0, 0), (N, N))

def tile(img, r=56, inset=6):
    """Rounded-square app tile covering the canvas."""
    w, h = img.w, img.h
    return lambda x, y: sd_round_box(x, y, w / 2, h / 2,
                                     w / 2 - inset, h / 2 - inset, r)

def base_tile(c0, c1):
    img = Img(N, N)
    img.fill(tile(img), grad(c0, c1, (0, 0), (N, N)))
    return img

# shared field geometry: a rounded square lens = the app's own search box
FCX, FCY, FHW, FR = 104, 96, 46, 16
def field_lens(x, y):
    return sd_round_box(x, y, FCX, FCY, FHW, FHW, FR)

def field_handle(x, y):
    return sd_segment(x, y, 131, 123, 184, 176, 26)

def v1_field():
    """The lens IS the app's search field. Geometric, unlike any generic magnifier."""
    img = base_tile(BLUE0, BLUE1)
    W = hexc('#FFFFFF')
    img.fill(field_lens, W)
    img.fill(field_handle, W)
    return img

def v2_caret():
    """Round lens carrying a text caret - keyboard-first, the essence of double-Ctrl."""
    img = base_tile(BLUE0, BLUE1)
    W = hexc('#FFFFFF')
    cx, cy, r = 104, 96, 54
    img.fill(lambda x, y: sd_circle(x, y, cx, cy, r), W)
    img.fill(lambda x, y: sd_segment(x, y, 142, 134, 192, 184, 24), W)
    # knockout painted with the tile gradient -> seamless, no colour guessing
    img.fill(lambda x, y: sd_round_box(x, y, cx + 1, cy, 3.0, 18, 3), TILE_BLUE)
    return img

def v3_bolt():
    """A bolt knocked out of the lens - 'swift' without adding a second colour."""
    img = base_tile(BLUE0, BLUE1)
    W = hexc('#FFFFFF')
    cx, cy, r = 104, 98, 50
    img.fill(lambda x, y: sd_circle(x, y, cx, cy, r), W)
    img.fill(lambda x, y: sd_segment(x, y, 140, 134, 188, 182, 24), W)
    pts = bolt_points(cx, cy + 1, 58, 0)
    img.fill(lambda x, y: sd_polygon(x, y, pts), TILE_BLUE)
    return img

def v3_bolt_small():
    """v3 minus the bolt. At 16-24px the bolt collapses into a stray dot and the
    lens reads as a ring, so small entries get the plain silhouette instead."""
    img = base_tile(BLUE0, BLUE1)
    W = hexc('#FFFFFF')
    cx, cy, r = 104, 98, 50
    img.fill(lambda x, y: sd_circle(x, y, cx, cy, r), W)
    img.fill(lambda x, y: sd_segment(x, y, 140, 134, 188, 182, 24), W)
    return img

def v4_ring():
    """Classic outline lens - the most legible silhouette at 16px."""
    img = base_tile(BLUE0, BLUE1)
    W = hexc('#FFFFFF')
    cx, cy, r = 102, 96, 46
    img.fill(lambda x, y: sd_ring(x, y, cx, cy, r, 18), W)
    img.fill(lambda x, y: sd_segment(x, y, 138, 132, 190, 184, 24), W)
    return img

def v5_glass():
    """Dark tile, gradient glass lens, white rim + specular. Premium."""
    img = base_tile(INK0, INK1)
    cx, cy, r = 104, 98, 48
    img.fill(lambda x, y: sd_circle(x, y, cx, cy, r + 7), hexc('#FFFFFF'))
    img.fill(lambda x, y: sd_circle(x, y, cx, cy, r),
             grad(hexc('#7FE6FF'), hexc('#2A5BFF'), (62, 52), (150, 146)))
    img.fill(lambda x, y: sd_segment(x, y, 141, 135, 190, 184, 24), hexc('#FFFFFF'))
    img.fill(lambda x, y: sd_circle(x, y, 86, 80, 15), hexc('#FFFFFF', 0.62))
    return img

def v6_mono():
    """Tray-safe single colour: field glyph with a real transparent caret slot."""
    img = Img(N, N)
    W = hexc('#FFFFFF')
    caret = lambda x, y: sd_round_box(x, y, FCX + 1, FCY, 3.0, 18, 3)
    img.fill(lambda x, y: sub(field_lens(x, y), caret(x, y)), W)
    img.fill(field_handle, W)
    return img

VARIANTS = [v1_field, v2_caret, v3_bolt, v4_ring, v5_glass, v6_mono]
LABELS = ['field', 'caret', 'bolt', 'ring', 'glass', 'mono']

# ---------------------------------------------------------------- contact sheet
FONT = {
    '1': ['010', '110', '010', '010', '111'],
    '2': ['111', '001', '111', '100', '111'],
    '3': ['111', '001', '111', '001', '111'],
    '4': ['101', '101', '111', '001', '001'],
    '5': ['111', '100', '111', '001', '111'],
    '6': ['111', '100', '111', '101', '111'],
}

def draw_digit(img, ch, x, y, scale, col):
    glyph = FONT[ch]
    for ry, row in enumerate(glyph):
        for rx, c in enumerate(row):
            if c != '1':
                continue
            for sy in range(scale):
                for sx in range(scale):
                    px, py = x + rx * scale + sx, y + ry * scale + sy
                    if 0 <= px < img.w and 0 <= py < img.h:
                        i = py * img.w + px
                        a = col[3]
                        img.r[i] = col[0] * a
                        img.g[i] = col[1] * a
                        img.b[i] = col[2] * a
                        img.a[i] = a

def plate(size, col, r=18):
    p = Img(size, size)
    sdf = lambda x, y: sd_round_box(x, y, size / 2, size / 2, size / 2, size / 2, r)
    p.fill(sdf, col)
    return p

def build_sheet():
    cell_w, cell_h = 340, 344
    cols, rows = 3, 2
    sheet = Img(cell_w * cols, cell_h * rows)
    bg = hexc('#EDEFF3')
    for i in range(sheet.w * sheet.h):
        sheet.r[i] = bg[0]; sheet.g[i] = bg[1]; sheet.b[i] = bg[2]; sheet.a[i] = 1.0

    big = 160
    for idx, fn in enumerate(VARIANTS):
        icon = fn()
        cx = (idx % cols) * cell_w
        cy = (idx // cols) * cell_h
        transparent = icon.is_transparent()

        draw_digit(sheet, str(idx + 1), cx + 18, cy + 14, 4, hexc('#7C8391'))

        # large preview, on a light card (or a dark plate for transparent glyphs)
        pad = 10
        canvas = Img(big + pad * 2, big + pad * 2)
        if transparent:
            canvas.blit(plate(big + pad * 2, hexc('#22262E'), 20), 0, 0)
        canvas.blit(downscale(icon, big), pad, pad)
        sheet.blit(canvas, cx + (cell_w - canvas.w) // 2, cy + 46)

        # strip: 48 / 32 / 16 plus a 4x zoom of the 16px render
        strip_y = cy + 46 + canvas.h + 14
        strip_h = 56
        dark = hexc('#1E222A')
        for yy in range(strip_y, strip_y + strip_h):
            for xx in range(cx + 26, cx + cell_w - 26):
                if 0 <= xx < sheet.w and yy < sheet.h:
                    i = yy * sheet.w + xx
                    sheet.r[i] = dark[0]; sheet.g[i] = dark[1]
                    sheet.b[i] = dark[2]; sheet.a[i] = 1.0

        x = cx + 40
        for s in (48, 32, 16):
            small = downscale(icon, s)
            sheet.blit(small, x, strip_y + (strip_h - s) // 2)
            x += s + 20
        z = upscale_nn(downscale(icon, 16), 64)
        sheet.blit(z, x + 2, strip_y + (strip_h - 64) // 2)
    return sheet

if __name__ == '__main__':
    for i, fn in enumerate(VARIANTS):
        img = fn()
        img.save(os.path.join(OUT_DIR, 'v%d.png' % (i + 1)))
        print('rendered v%d %s' % (i + 1, LABELS[i]))
    sheet = build_sheet()
    sheet.save(os.path.join(OUT_DIR, 'icon-sheet.png'))
    print('sheet:', sheet.w, 'x', sheet.h)
