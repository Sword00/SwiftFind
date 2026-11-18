"""Render the shipped icon: 256px hero plus the real ICO sizes on light and dark."""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import icon_studio as S

HERE = os.path.dirname(os.path.abspath(__file__))

def band(sheet, x, y, w, h, col):
    for yy in range(y, y + h):
        for xx in range(x, x + w):
            if 0 <= xx < sheet.w and 0 <= yy < sheet.h:
                i = yy * sheet.w + xx
                sheet.r[i] = col[0]; sheet.g[i] = col[1]
                sheet.b[i] = col[2]; sheet.a[i] = 1.0

def main():
    big = S.v3_bolt()
    small = S.v3_bolt_small()

    W, H = 720, 470
    sheet = S.Img(W, H)
    band(sheet, 0, 0, W, H, S.hexc('#F4F5F8'))

    hero = S.downscale(big, 256)
    sheet.blit(hero, 52, 48)

    # real sizes, light background
    band(sheet, 380, 60, 300, 96, S.hexc('#FFFFFF'))
    x = 400
    for size in (64, 48, 32, 24, 16):
        src = small if size <= 24 else big
        sheet.blit(S.downscale(src, size), x, 60 + (96 - size) // 2)
        x += size + 22

    # real sizes, dark background
    band(sheet, 380, 176, 300, 96, S.hexc('#1E222A'))
    x = 400
    for size in (64, 48, 32, 24, 16):
        src = small if size <= 24 else big
        sheet.blit(S.downscale(src, size), x, 176 + (96 - size) // 2)
        x += size + 22

    # tray strip at true 16px on the real taskbar colour
    band(sheet, 52, 330, 628, 44, S.hexc('#202020'))
    sheet.blit(S.downscale(small, 16), 68, 344)
    out = os.path.join(HERE, 'icon-final.png')
    sheet.save(out)
    print('wrote', out, '%dx%d' % (W, H))

if __name__ == '__main__':
    main()
