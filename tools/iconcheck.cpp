// iconcheck - load resource icon #1 out of an exe and render it at several
// sizes, reporting coverage so a broken ICO/BMP/PNG entry is caught before
// the user ever sees it.
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <cstdio>
#include <cwchar>

static void attachConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD t = GetFileType(h);
        if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        freopen("CONOUT$", "w", stdout);
    }
}

// Draw the icon onto a magenta field and report how much survived.
static void probe(HMODULE mod, int size) {
    HICON ic = (HICON)LoadImageW(mod, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                 size, size, LR_DEFAULTCOLOR);
    if (!ic) {
        wprintf(L"%4dpx  LOAD FAILED (err %lu)\n", size, GetLastError());
        return;
    }
    ICONINFO ii{};
    GetIconInfo(ic, &ii);
    BITMAP bm{};
    GetObjectW(ii.hbmColor, sizeof(bm), &bm);
    wprintf(L"%4dpx  loaded  bitmap=%ldx%ld  planes=%u  bpp=%u",
            size, bm.bmWidth, bm.bmHeight, bm.bmPlanes, bm.bmBitsPixel);

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;      // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, dib);

    RECT rc{0, 0, size, size};
    HBRUSH mg = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(dc, &rc, mg);
    DrawIconEx(dc, 0, 0, ic, size, size, 0, nullptr, DI_NORMAL);

    const unsigned* p = (const unsigned*)bits;
    int painted = 0, transparent = 0, white = 0, blue = 0;
    for (int i = 0; i < size * size; ++i) {
        unsigned v = p[i];
        int b = v & 0xff, g = (v >> 8) & 0xff, r = (v >> 16) & 0xff;
        if (r == 255 && g == 0 && b == 255) { transparent++; continue; }
        painted++;
        if (r > 230 && g > 230 && b > 230) white++;
        if (b > 120 && b > r + 40) blue++;
    }
    wprintf(L"  painted=%d  transparent=%d  white=%d  blue=%d\n",
            painted, transparent, white, blue);

    SelectObject(dc, old);
    DeleteObject(dib);
    DeleteObject(mg);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    DestroyIcon(ic);
}

int wmain(int argc, wchar_t** argv) {
    attachConsole();
    const wchar_t* path = argc > 1 ? argv[1] : L"src\\SwiftFind.exe";
    HMODULE mod = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (!mod) {
        wprintf(L"cannot open %ls (err %lu)\n", path, GetLastError());
        return 1;
    }
    wprintf(L"icon resource probe: %ls\n", path);
    int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    for (int s : sizes) probe(mod, s);
    FreeLibrary(mod);
    return 0;
}
