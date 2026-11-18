"""Compare the 16/20/24px renderings of v3 with and without the bolt."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_studio as S

W = S.hexc('#FFFFFF')

def v3_with():
    return S.v3_bolt()

def v3_without():
    img = S.base_tile(S.BLUE0, S.BLUE1)
    cx, cy, r = 104, 98, 50
    img.fill(lambda x, y: S.sd_circle(x, y, cx, cy, r), W)
    img.fill(lambda x, y: S.sd_segment(x, y, 140, 134, 188, 182, 24), W)
    return img

def strip(img, size, f=12):
    return S.upscale_nn(S.downscale(img, size), size * f)

if __name__ == '__main__':
    a, b = v3_with(), v3_without()
    rows = [(16, 14), (20, 12), (24, 10), (32, 8)]
    pad = 24
    cellh = max(s * f for s, f in rows) + pad * 2
    cellw = max(s * f for s, f in rows) + pad * 2
    sheet = S.Img(cellw * 2 + pad, cellh * len(rows) + pad)
    bg = S.hexc('#1E222A')
    for i in range(sheet.w * sheet.h):
        sheet.r[i] = bg[0]; sheet.g[i] = bg[1]; sheet.b[i] = bg[2]; sheet.a[i] = 1.0
    for ri, (size, f) in enumerate(rows):
        for ci, icon in enumerate((a, b)):
            z = strip(icon, size, f)
            sheet.blit(z, pad // 2 + ci * cellw + (cellw - z.w) // 2,
                       pad // 2 + ri * cellh + (cellh - z.h) // 2)
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'small-compare.png')
    sheet.save(out)
    print('wrote', out, '%dx%d  left=with bolt  right=without' % (sheet.w, sheet.h))
