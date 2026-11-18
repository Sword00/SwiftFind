// wins - enumerate top-level windows so we can see what actually exists
// instead of guessing from a failed FindWindow.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>

static void attachConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD t = GetFileType(h);
        if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
        freopen("CONOUT$", "w", stdout);
}

struct Ctx { DWORD pid; int shown; };

static BOOL CALLBACK enumProc(HWND h, LPARAM lp) {
    Ctx* c = (Ctx*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (c->pid && pid != c->pid) return TRUE;
    wchar_t cls[128] = L"", title[256] = L"";
    GetClassNameW(h, cls, 128);
    GetWindowTextW(h, title, 256);
    RECT r{};
    GetWindowRect(h, &r);
    wprintf(L"hwnd=%p pid=%lu vis=%d class=%-22ls rect=%d,%d %dx%d title=\"%ls\"\n",
            (void*)h, pid, IsWindowVisible(h) ? 1 : 0, cls,
            r.left, r.top, r.right - r.left, r.bottom - r.top, title);
    c->shown++;
    return TRUE;
}

int wmain(int argc, wchar_t** argv) {
    attachConsole();
    Ctx c{ argc > 1 ? (DWORD)_wtoi(argv[1]) : 0, 0 };
    wprintf(L"--- top-level windows%s ---\n",
            c.pid ? L" for pid" : L" (all)");
    EnumWindows(enumProc, (LPARAM)&c);
    wprintf(L"--- %d window(s) ---\n", c.shown);
    return 0;
}
