// Minimal screen capture -> BMP. Used to verify SwiftFind's UI.
// usage: shot.exe [x y w h] out.bmp        capture screen rect
//        shot.exe --win "Title" out.bmp    capture a window by title
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

static bool capture(int x, int y, int w, int h, const wchar_t* outPath) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, w, h, screen, x, y, SRCCOPY);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    int stride = ((w * 3 + 3) / 4) * 4;
    int imgSize = stride * h;
    unsigned char* px = new unsigned char[imgSize];
    GetDIBits(mem, bmp, 0, h, px, &bi, DIB_RGB_COLORS);

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    if (!ok) { delete[] px; return false; }

    FILE* f = _wfopen(outPath, L"wb");
    if (!f) { delete[] px; return false; }
    BITMAPFILEHEADER fh{};
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fh.bfSize = fh.bfOffBits + imgSize;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&bi.bmiHeader, sizeof(BITMAPINFOHEADER), 1, f);
    fwrite(px, imgSize, 1, f);
    fclose(f);
    delete[] px;
    return true;
}

int wmain(int argc, wchar_t** argv) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    wprintf(L"screen %dx%d\n", screenW, screenH);

    if (argc >= 3 && wcscmp(argv[1], L"--check") == 0) {
        HWND h = FindWindowW(nullptr, argv[2]);
        if (!h) { printf("MISSING\n"); return 4; }
        printf("%s\n", IsWindowVisible(h) ? "VISIBLE" : "HIDDEN");
        return 0;
    }

    if (argc >= 4 && wcscmp(argv[1], L"--win") == 0) {
        HWND h = FindWindowW(nullptr, argv[2]);
        if (!h) { wprintf(L"window not found\n"); return 4; }
        RECT r;
        GetWindowRect(h, &r);
        int pad = 12;
        int x = r.left - pad, y = r.top - pad;
        int w = (r.right - r.left) + pad * 2, hh = (r.bottom - r.top) + pad * 2;
        wprintf(L"window rect %d,%d %dx%d\n", r.left, r.top, r.right - r.left, r.bottom - r.top);

        // raise without stealing focus so nothing overlaps the capture
        SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        Sleep(200);
        bool ok = capture(x, y, w, hh, argv[3]);
        SetWindowPos(h, HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        wprintf(ok ? L"ok\n" : L"failed\n");
        return ok ? 0 : 1;
    }

    int x = 0, y = 0, w = screenW, h = screenH;
    if (argc >= 5) {
        x = _wtoi(argv[1]); y = _wtoi(argv[2]);
        w = _wtoi(argv[3]); h = _wtoi(argv[4]);
    }
    const wchar_t* outPath = (argc >= 6) ? argv[5] : (argc >= 2 ? argv[1] : L"shot.bmp");
    bool ok = capture(x, y, w, h, outPath);
    wprintf(ok ? L"ok\n" : L"failed\n");
    return ok ? 0 : 1;
}
