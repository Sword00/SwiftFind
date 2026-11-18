// SwiftFind - a tiny Listary-like launcher for Windows
// Single-file Win32 app: full-disk index + Start Menu apps + fuzzy search
// Build: g++ -O2 -s -municode -mwindows -std=c++17 main.cpp res.o -o SwiftFind.exe ...

#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <memory>

#include <atomic>

#include <algorithm>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------- constants
#define APP_NAME        L"SwiftFind"
#define ID_EDIT         1001
#define ID_LIST         1002
#define WM_TRAY         (WM_APP + 1)
#define WM_TOGGLE       (WM_APP + 2)
#define WM_REBUILT      (WM_APP + 3)
#define WM_BUILDPROG    (WM_APP + 4)

#define INDEX_MAGIC     "QFDX"
#define INDEX_VERSION   3u
#define MAX_RESULTS     60

// ---------------------------------------------------------------- globals
static HINSTANCE   g_hInst      = nullptr;
static HWND        g_hwnd       = nullptr;
static HWND        g_hEdit      = nullptr;
static HWND        g_hList      = nullptr;
static HFONT       g_fEdit      = nullptr;
static HFONT       g_fName      = nullptr;
static HFONT       g_fPath      = nullptr;
static HFONT       g_fTitle     = nullptr;
static int         g_dpi        = 96;
static HHOOK       g_hook       = nullptr;
static DWORD       g_lastCtrl   = 0;
static bool        g_visible    = false;
static bool        g_trayAdded  = false;
static bool        g_showTray   = true;   // 设置项：是否显示托盘图标
static std::wstring g_indexPath;
static HICON       g_appIcon    = nullptr;
static std::wstring g_startQuery;
static bool        g_noHide     = false;   // debug: never auto-hide on focus loss
static HWND        g_hSettings  = nullptr;
static bool        g_hideOnBlur  = true;   // user setting
static bool        g_followMouse = false;  // user setting: open at cursor

struct Result { uint32_t idx; int32_t score; };
static std::vector<Result> g_res;

static std::atomic<uint64_t> g_buildCount{ 0 };
static std::atomic<bool>     g_building{ false };
static std::atomic<int>      g_buildPhase{ 0 };   // 0 idle 1 scanning 2 saving

static inline int S(int v) { return MulDiv(v, g_dpi, 96); }

static std::wstring dataDir();   // defined near the bottom

// ---------------------------------------------------------------- theme
// Every colour the app draws comes from here. Two palettes, resolved once per
// theme change so the paint code never branches on light/dark.
struct Theme {
    COLORREF bg;
    COLORREF border;
    COLORREF text;
    COLORREF textDim;
    COLORREF selBg;
    COLORREF sep;
    COLORREF btnBg;
    COLORREF btnBorder;
    COLORREF accent;
    COLORREF toggleOff;
    COLORREF toggleOffBorder;
    COLORREF knob;
    COLORREF segBg;
    COLORREF segBorder;
};

static Theme  g_theme;
static bool   g_dark      = false;
static int    g_themeMode = 2;      // 0 = light, 1 = dark, 2 = follow system
static HBRUSH g_brBg      = nullptr;

static bool systemIsDark() {
    DWORD v = 1, cb = sizeof(v);
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        RegQueryValueExW(k, L"AppsUseLightTheme", nullptr, nullptr, (LPBYTE)&v, &cb);
        RegCloseKey(k);
    }
    return v == 0;
}

// Native popup menus are drawn by the OS, so they ignore our palette. Opting
// into the undocumented dark app mode is the only way to make them match.
// Ordinals moved between Windows builds, so try each and give up quietly.
static void setNativeMenuTheme(bool dark) {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    typedef int (WINAPI *SetPreferredAppMode)(int);
    const char* ord[] = { MAKEINTRESOURCEA(135), MAKEINTRESOURCEA(136),
                          MAKEINTRESOURCEA(104) };
    for (const char* o : ord) {
        auto fn = (SetPreferredAppMode)GetProcAddress(ux, o);
        if (fn) { fn(dark ? 2 : 3); return; }   // 2 = ForceDark, 3 = ForceLight
    }
}

static void applyTheme() {
    g_dark = (g_themeMode == 1) || (g_themeMode == 2 && systemIsDark());
    setNativeMenuTheme(g_dark);

    if (g_dark) {
        g_theme.bg              = RGB(32, 34, 38);
        g_theme.border          = RGB(58, 62, 70);
        g_theme.text            = RGB(232, 234, 238);
        g_theme.textDim         = RGB(138, 145, 157);
        g_theme.selBg           = RGB(44, 52, 68);
        g_theme.sep             = RGB(52, 56, 64);
        g_theme.btnBg           = RGB(46, 50, 58);
        g_theme.btnBorder       = RGB(66, 71, 80);
        g_theme.accent          = RGB(64, 124, 255);
        g_theme.toggleOff       = RGB(72, 77, 86);
        g_theme.toggleOffBorder = RGB(84, 90, 100);
        g_theme.knob            = RGB(236, 239, 244);
        g_theme.segBg           = RGB(44, 48, 56);
        g_theme.segBorder       = RGB(62, 67, 76);
    } else {
        g_theme.bg              = RGB(252, 252, 253);
        g_theme.border          = RGB(214, 218, 224);
        g_theme.text            = RGB(26, 28, 32);
        g_theme.textDim         = RGB(146, 152, 162);
        g_theme.selBg           = RGB(233, 241, 253);
        g_theme.sep             = RGB(238, 240, 243);
        g_theme.btnBg           = RGB(243, 245, 248);
        g_theme.btnBorder       = RGB(214, 219, 226);
        g_theme.accent          = RGB(45, 104, 255);
        g_theme.toggleOff       = RGB(206, 210, 218);
        g_theme.toggleOffBorder = RGB(198, 203, 211);
        g_theme.knob            = RGB(255, 255, 255);
        g_theme.segBg           = RGB(240, 242, 246);
        g_theme.segBorder       = RGB(220, 224, 231);
    }

    if (g_brBg) { DeleteObject(g_brBg); g_brBg = nullptr; }
    g_brBg = CreateSolidBrush(g_theme.bg);
}

// ---------------------------------------------------------------- utf helpers
static std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring toWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::wstring parentOf(const std::wstring& p) {
    size_t i = p.find_last_of(L"\\/");
    if (i == std::wstring::npos) return std::wstring();
    if (i == 2 && p.size() > 1 && p[1] == L':') return p.substr(0, 3);
    return p.substr(0, i);
}

// ---------------------------------------------------------------- index model
// Paths are stored compactly: unique directories in dirArena, basenames in
// nameArena, entries reference both. ~9-12 bytes per item plus name text.
struct Entry {
    uint32_t dir;
    uint32_t name;
    uint8_t  kind;   // 0 = file, 1 = dir, 2 = app (.lnk)
    uint8_t  pad[3];
};

struct Index {
    std::string           dirArena;
    std::vector<uint32_t> dirOff;
    std::string           nameArena;
    std::vector<Entry>    ents;

    const char* dirOf(const Entry& e) const  { return dirArena.c_str() + dirOff[e.dir]; }
    const char* nameOf(const Entry& e) const { return nameArena.c_str() + e.name; }

    std::wstring fullPath(uint32_t i) const {
        const Entry& e = ents[i];
        std::string p;
        p.reserve(160);
        p += dirOf(e);
        if (!p.empty() && p.back() != '\\') p += '\\';
        p += nameOf(e);
        return toWide(p);
    }
};

static std::shared_ptr<Index> g_index;
// SRWLOCK instead of std::mutex: zero-initialised at compile time and it does
// not pull libpthread's mutex/cond machinery into the binary.
static SRWLOCK g_indexLock = SRWLOCK_INIT;

struct IndexLock {
    IndexLock()  { AcquireSRWLockExclusive(&g_indexLock); }
    ~IndexLock() { ReleaseSRWLockExclusive(&g_indexLock); }
};

static std::shared_ptr<Index> getIndex() {
    IndexLock lk;
    return g_index;
}

static void setIndex(std::shared_ptr<Index> ix) {
    IndexLock lk;
    g_index = std::move(ix);
}

// ---------------------------------------------------------------- builder
struct BuildCtx {
    std::string                          dirArena;
    std::vector<uint32_t>                dirOff;
    std::string                          nameArena;
    std::vector<Entry>                   ents;
    std::vector<std::pair<std::string, uint32_t>> dirMap;  // insertion-ordered, linear probe

    uint32_t addDir(const std::string& d) {
        for (auto& kv : dirMap)
            if (kv.first == d) return kv.second;
        uint32_t idx = (uint32_t)dirOff.size();
        dirOff.push_back((uint32_t)dirArena.size());
        dirArena += d;
        dirArena.push_back('\0');
        dirMap.emplace_back(d, idx);
        return idx;
    }

    void addEntry(uint32_t dirIdx, const wchar_t* name, uint8_t kind) {
        Entry e;
        e.dir  = dirIdx;
        e.name = (uint32_t)nameArena.size();
        e.kind = kind;
        e.pad[0] = e.pad[1] = e.pad[2] = 0;

        int n = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            size_t old = nameArena.size();
            nameArena.resize(old + (size_t)n);
            WideCharToMultiByte(CP_UTF8, 0, name, -1, &nameArena[old], n, nullptr, nullptr);
            nameArena.pop_back();               // drop trailing NUL from the resize
            nameArena.push_back('\0');          // keep exactly one
            ents.push_back(e);
        }
    }
};

// long-path safe prefix
static std::wstring fixPath(const std::wstring& p) {
    if (p.size() >= 240 && p.compare(0, 4, L"\\\\?\\") != 0)
        return L"\\\\?\\" + p;
    return p;
}

static void walkDir(BuildCtx& c, const std::wstring& dirW) {
    uint32_t dirIdx = c.addDir(toUtf8(dirW));

    std::wstring pattern = fixPath(dirW);
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> subs;
    subs.reserve(32);

    do {
        const wchar_t* nm = fd.cFileName;
        if (nm[0] == L'.' && (nm[1] == 0 || (nm[1] == L'.' && nm[2] == 0))) continue;

        DWORD a = fd.dwFileAttributes;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) continue;   // no junction / symlink loops

        if (a & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(nm, L"$Recycle.Bin") == 0) continue;
            if (wcscmp(nm, L"System Volume Information") == 0) continue;
            c.addEntry(dirIdx, nm, 1);
            subs.push_back(nm);
        } else {
            c.addEntry(dirIdx, nm, 0);
        }
        if ((c.ents.size() & 0x3FFF) == 0) g_buildCount.store(c.ents.size());
    } while (FindNextFileW(h, &fd));

    FindClose(h);

    for (auto& s : subs) walkDir(c, dirW + L"\\" + s);
}

static void enumAppsIn(BuildCtx& c, const std::wstring& root) {
    if (root.empty()) return;
    std::wstring pattern = fixPath(root) + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;

    std::vector<std::wstring> subs;
    do {
        const wchar_t* nm = fd.cFileName;
        if (nm[0] == L'.' && (nm[1] == 0 || (nm[1] == L'.' && nm[2] == 0))) continue;
        DWORD a = fd.dwFileAttributes;
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (a & FILE_ATTRIBUTE_DIRECTORY) {
            subs.push_back(nm);
        } else {
            size_t len = wcslen(nm);
            if (len > 4 && _wcsicmp(nm + len - 4, L".lnk") == 0) {
                uint32_t d = c.addDir(toUtf8(root));
                c.addEntry(d, nm, 2);
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    for (auto& s : subs) enumAppsIn(c, root + L"\\" + s);
}

static void mergeCtx(BuildCtx& dst, BuildCtx& src) {
    uint32_t dirBase  = (uint32_t)dst.dirOff.size();
    uint32_t nameBase = (uint32_t)dst.nameArena.size();

    for (auto& off : src.dirOff) dst.dirOff.push_back(off + (uint32_t)dst.dirArena.size());
    dst.dirArena += src.dirArena;

    dst.nameArena += src.nameArena;

    dst.ents.reserve(dst.ents.size() + src.ents.size());
    for (auto e : src.ents) {
        e.dir  += dirBase;
        e.name += nameBase;
        dst.ents.push_back(e);
    }
}

static bool saveIndex(const Index& ix) {
    HANDLE h = CreateFileW(g_indexPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    struct Header {
        char     magic[4];
        uint32_t version;
        uint32_t entCount;
        uint32_t dirCount;
        uint32_t pad;
        uint64_t dirArenaSize;
        uint64_t nameArenaSize;
        uint64_t builtAt;
    } hd;
    memcpy(hd.magic, INDEX_MAGIC, 4);
    hd.version      = INDEX_VERSION;
    hd.entCount     = (uint32_t)ix.ents.size();
    hd.dirCount     = (uint32_t)ix.dirOff.size();
    hd.pad          = 0;
    hd.dirArenaSize = ix.dirArena.size();
    hd.nameArenaSize= ix.nameArena.size();

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    hd.builtAt = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;

    DWORD wrote = 0;
    bool ok = WriteFile(h, &hd, sizeof(hd), &wrote, nullptr)
           && WriteFile(h, ix.dirArena.data(), (DWORD)ix.dirArena.size(), &wrote, nullptr)
           && WriteFile(h, ix.nameArena.data(), (DWORD)ix.nameArena.size(), &wrote, nullptr)
           && WriteFile(h, ix.dirOff.data(), (DWORD)(ix.dirOff.size() * 4), &wrote, nullptr)
           && WriteFile(h, ix.ents.data(), (DWORD)(ix.ents.size() * sizeof(Entry)), &wrote, nullptr);
    CloseHandle(h);
    return ok;
}

static std::shared_ptr<Index> loadIndex() {
    HANDLE h = CreateFileW(g_indexPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;

    struct Header {
        char     magic[4];
        uint32_t version;
        uint32_t entCount;
        uint32_t dirCount;
        uint32_t pad;
        uint64_t dirArenaSize;
        uint64_t nameArenaSize;
        uint64_t builtAt;
    } hd;

    DWORD got = 0;
    if (!ReadFile(h, &hd, sizeof(hd), &got, nullptr) || got != sizeof(hd)
        || memcmp(hd.magic, INDEX_MAGIC, 4) != 0 || hd.version != INDEX_VERSION) {
        CloseHandle(h);
        return nullptr;
    }

    auto ix = std::make_shared<Index>();
    ix->dirArena.resize((size_t)hd.dirArenaSize);
    ix->nameArena.resize((size_t)hd.nameArenaSize);
    ix->dirOff.resize(hd.dirCount);
    ix->ents.resize(hd.entCount);

    bool ok = ReadFile(h, &ix->dirArena[0], (DWORD)hd.dirArenaSize, &got, nullptr)
           && ReadFile(h, &ix->nameArena[0], (DWORD)hd.nameArenaSize, &got, nullptr)
           && ReadFile(h, ix->dirOff.data(), (DWORD)(ix->dirOff.size() * 4), &got, nullptr)
           && ReadFile(h, ix->ents.data(), (DWORD)(ix->ents.size() * sizeof(Entry)), &got, nullptr);
    CloseHandle(h);
    if (!ok) return nullptr;
    return ix;
}

// Raw Win32 threads instead of std::thread: the libstdc++ thread wrapper drags
// in libpthread (thread/cond/rwlock/mutex/sched = ~23 KB) for what is three
// fire-and-join call sites.
struct WalkJob { BuildCtx* ctx; const std::wstring* root; };

static DWORD WINAPI walkJobProc(void* p) {
    WalkJob* j = (WalkJob*)p;
    walkDir(*j->ctx, *j->root);
    return 0;
}

static void buildIndexBlocking(bool alsoApps = true) {
    g_building.store(true);
    g_buildPhase.store(1);
    g_buildCount.store(0);

    DWORD drives = GetLogicalDrives();
    std::vector<std::wstring> roots;
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        wchar_t r[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        if (GetDriveTypeW(r) == DRIVE_FIXED) {
            r[2] = 0;                     // "C:"
            roots.push_back(r);
        }
    }

    std::vector<BuildCtx> local(roots.size());
    std::vector<WalkJob> jobs(roots.size());
    std::vector<HANDLE> hs;
    hs.reserve(roots.size());

    for (size_t i = 0; i < roots.size(); ++i) {
        jobs[i] = { &local[i], &roots[i] };
        HANDLE h = CreateThread(nullptr, 0, walkJobProc, &jobs[i], 0, nullptr);
        if (h) hs.push_back(h);
    }
    if (!hs.empty())
        WaitForMultipleObjects((DWORD)hs.size(), hs.data(), TRUE, INFINITE);
    for (HANDLE h : hs) CloseHandle(h);

    BuildCtx all;
    uint64_t total = 0;
    for (auto& c : local) { total += c.ents.size(); mergeCtx(all, c); }
    local.clear();
    local.shrink_to_fit();

    if (alsoApps) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH);
        if (n && n < MAX_PATH) {
            std::wstring p = std::wstring(buf) + L"\\Microsoft\\Windows\\Start Menu\\Programs";
            BuildCtx ac; enumAppsIn(ac, p); mergeCtx(all, ac);
        }
        n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
        if (n && n < MAX_PATH) {
            std::wstring p = std::wstring(buf) + L"\\Microsoft\\Windows\\Start Menu\\Programs";
            BuildCtx ac; enumAppsIn(ac, p); mergeCtx(all, ac);
        }
    }

    auto ix = std::make_shared<Index>();
    ix->dirArena.swap(all.dirArena);
    ix->dirOff.swap(all.dirOff);
    ix->nameArena.swap(all.nameArena);
    ix->ents.swap(all.ents);

    g_buildPhase.store(2);
    saveIndex(*ix);

    setIndex(ix);
    g_buildPhase.store(0);
    g_building.store(false);
}

// ---------------------------------------------------------------- matching
static inline char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Matching: contiguous substring first (strong signal), then gapped
// subsequence with a per-character gap penalty. This keeps "node" from
// matching "Wi(n)d(ow)s (De)fender" style scattered hits.
static inline bool isLaunchable(const char* s, int slen) {
    if (slen < 5) return false;
    const char* e = s + slen - 4;
    const char* want[5] = { ".exe", ".bat", ".cmd", ".com", ".lnk" };
    for (auto w : want) {
        bool ok = true;
        for (int k = 0; k < 4; ++k)
            if (lc(e[k]) != w[k]) { ok = false; break; }
        if (ok) return true;
    }
    return false;
}

static inline int matchScore(const char* s, const char* q) {
    int qlen = (int)strlen(q);
    if (qlen == 0) return 0;

    int slen = 0;
    while (s[slen]) ++slen;
    if (qlen > slen) return 0;

    // ---- 1) contiguous (case-insensitive) substring
    for (int i = 0; i + qlen <= slen; ++i) {
        int k = 0;
        while (k < qlen && lc(s[i + k]) == lc(q[k])) ++k;
        if (k == qlen) {
            int sc = 220;
            if (i == 0) sc += 130;                                   // prefix hit
            else {
                char p = s[i - 1];
                if (p == '-' || p == '_' || p == ' ' || p == '.' || p == '(') sc += 75;
            }
            if (slen == qlen)      sc += 90;                         // exact name
            else if (slen <= 16)   sc += 55;
            else if (slen <= 32)   sc += 25;
            else if (slen <= 60)   sc += 8;
            sc -= i * 2;                                             // later = slightly worse
            if (isLaunchable(s, slen)) sc += 45;                     // prefer things you can run
            return sc;
        }
    }

    // ---- 2) gapped subsequence
    int qi = 0, sc = 0, last = -1;
    for (int i = 0; i < slen; ++i) {
        if (lc(s[i]) != lc(q[qi])) continue;
        if (i == 0) sc += 18;
        else {
            char p = s[i - 1];
            if (p == '-' || p == '_' || p == ' ' || p == '.') sc += 9;
        }
        if (i == last + 1) sc += 8;
        else if (last >= 0) sc -= (i - last - 1) * 5;                // gap penalty
        sc += 3;
        last = i;
        if (!q[++qi]) {
            int span = last + 1;
            if (span <= 10)      sc += 30;
            else if (span <= 24) sc += 12;
            return sc > 0 ? sc : 0;
        }
    }
    return 0;
}

static void searchRange(const Index& ix, const std::string& q,
                        size_t begin, size_t end, std::vector<Result>& out) {
    const char* names = ix.nameArena.c_str();
    const char* qs = q.c_str();
    char q0 = lc(q[0]);
    char q0u = (q0 >= 'a' && q0 <= 'z') ? (char)(q0 - 32) : q0;

    for (size_t i = begin; i < end; ++i) {
        const char* s = names + ix.ents[i].name;
        // fast reject: name must contain the first query byte in either case
        bool has = false;
        for (const char* p = s; *p; ++p) {
            if (*p == q0 || *p == q0u) { has = true; break; }
        }
        if (!has) continue;

        int sc = matchScore(s, qs);
        if (sc > 0) {
            if (ix.ents[i].kind == 2) sc += 70;      // apps first
            else if (ix.ents[i].kind == 1) sc += 6;  // folders slightly above files
            out.push_back({ (uint32_t)i, sc });
        }
    }
}

struct SearchJob {
    const Index* ix;
    const std::string* q;
    size_t b, e;
    std::vector<Result>* out;
};

static DWORD WINAPI searchJobProc(void* p) {
    SearchJob* j = (SearchJob*)p;
    searchRange(*j->ix, *j->q, j->b, j->e, *j->out);
    return 0;
}

static void searchIndex(const Index& ix, const std::wstring& wq, std::vector<Result>& out) {
    out.clear();
    if (wq.empty() || ix.ents.empty()) return;

    std::string q = toUtf8(wq);
    for (auto& c : q) c = lc(c);

    size_t n = ix.ents.size();
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    unsigned hw = si.dwNumberOfProcessors;
    if (hw < 2) hw = 2;
    if (hw > 8) hw = 8;

    size_t chunk = (n + hw - 1) / hw;
    std::vector<std::vector<Result>> parts(hw);
    std::vector<SearchJob> jobs(hw);
    std::vector<HANDLE> hs;
    hs.reserve(hw);

    for (unsigned t = 0; t < hw; ++t) {
        size_t b = t * chunk;
        size_t e = std::min(n, b + chunk);
        if (b >= e) break;
        jobs[t] = { &ix, &q, b, e, &parts[t] };
        HANDLE h = CreateThread(nullptr, 0, searchJobProc, &jobs[t], 0, nullptr);
        if (h) hs.push_back(h);
    }
    if (!hs.empty())
        WaitForMultipleObjects((DWORD)hs.size(), hs.data(), TRUE, INFINITE);
    for (HANDLE h : hs) CloseHandle(h);

    size_t total = 0;
    for (auto& p : parts) total += p.size();
    out.reserve(total);
    for (auto& p : parts)
        for (auto& r : p) out.push_back(r);

    size_t keep = std::min<size_t>(MAX_RESULTS, out.size());
    std::partial_sort(out.begin(), out.begin() + keep, out.end(),
                      [](const Result& a, const Result& b) { return a.score > b.score; });
    out.resize(keep);
}

// ---------------------------------------------------------------- launching
static void launchPath(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring dir = parentOf(path);
    HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr,
                                dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        // fall back: reveal in explorer
        std::wstring arg = L"/select,\"" + path + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
}

// ---------------------------------------------------------------- window
static void refreshResults();

// A background process cannot normally call SetForegroundWindow. Attaching to
// the foreground thread's input queue is the standard way around it; without
// this the window pops up and immediately loses focus (and auto-hides).
static void forceForeground(HWND hwnd) {
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD myThread = GetCurrentThreadId();

    if (fgThread && fgThread != myThread) {
        AttachThreadInput(fgThread, myThread, TRUE);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);
        SetFocus(hwnd);
        AttachThreadInput(fgThread, myThread, FALSE);
    } else {
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
    }
}

static void hideWindow() {
    if (!g_visible) return;
    ShowWindow(g_hwnd, SW_HIDE);
    g_visible = false;
}

static void showWindow() {
    if (!g_hwnd) return;

    SetWindowTextW(g_hEdit, L"");
    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    g_res.clear();

    if (!g_startQuery.empty()) {
        SetWindowTextW(g_hEdit, g_startQuery.c_str());
        SendMessageW(g_hEdit, EM_SETSEL, 0, -1);
        g_startQuery.clear();
    }

    // decide compact vs expanded BEFORE positioning, so the window never
    // flashes at the wrong height
    refreshResults();

    // position: screen-centered by default, or at the cursor when enabled
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    int x, y;
    if (g_followMouse) {
        x = pt.x - S(90);      // put the caret roughly under the cursor
        y = pt.y - S(26);
    } else {
        x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
        y = mi.rcWork.top  + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
    }
    if (x + w > mi.rcWork.right)  x = mi.rcWork.right - w;
    if (x < mi.rcWork.left)       x = mi.rcWork.left;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (y < mi.rcWork.top)        y = mi.rcWork.top;

    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    ShowWindow(g_hwnd, SW_SHOW);
    forceForeground(g_hwnd);
    SetWindowPos(g_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetFocus(g_hEdit);
    g_visible = true;

    InvalidateRect(g_hwnd, nullptr, TRUE);
}

static void toggleWindow() {
    if (g_visible) hideWindow();
    else showWindow();
}

// ---------------------------------------------------------------- icon cache
// Real per-path shell icons, cached. Only visible rows ever ask for one.
struct IconCache {
    std::vector<std::pair<std::wstring, HICON>> items;

    HICON get(const std::wstring& path) {
        for (auto& kv : items)
            if (kv.first == path) return kv.second;

        SHFILEINFOW sfi{};
        HICON ic = nullptr;
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON | SHGFI_SHELLICONSIZE))
            ic = sfi.hIcon;

        if (!ic) {
            // fall back to an extension-only guess
            size_t dot = path.find_last_of(L'.');
            std::wstring ext = (dot == std::wstring::npos) ? L"" : path.substr(dot);
            if (!ext.empty() && SHGetFileInfoW(ext.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi,
                                               sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON |
                                               SHGFI_USEFILEATTRIBUTES))
                ic = sfi.hIcon;
        }
        if (!ic) ic = LoadIconW(nullptr, IDI_APPLICATION);

        if (items.size() > 600) {
            for (auto& kv : items)
                if (kv.second && kv.second != ic) DestroyIcon(kv.second);
            items.clear();
        }
        items.emplace_back(path, ic);
        return ic;
    }
};
static IconCache g_icons;

// ---------------------------------------------------------------- painting
static void paintFrame(HDC dc, const RECT& rc) {
    FillRect(dc, &rc, g_brBg);

    HPEN pen = CreatePen(PS_SOLID, 1, g_theme.border);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    HGDIOBJ oldBr  = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(dc, oldBr);
    SelectObject(dc, oldPen);
    DeleteObject(pen);
}

// Hand-rolled instead of swprintf: the printf family costs ~20 KB of
// __pformat + __gdtoa, and we only ever print unsigned integers here.
static std::wstring utoW(uint64_t v) {
    wchar_t raw[24];
    int n = 0;
    do { raw[n++] = wchar_t(L'0' + (v % 10)); v /= 10; } while (v);
    std::wstring out;
    out.reserve(n);
    while (n) out += raw[--n];
    return out;
}

static std::wstring withCommas(uint64_t v) {
    std::wstring d = utoW(v), out;
    int n = (int)d.size();
    for (int i = 0; i < n; ++i) {
        if (i && ((n - i) % 3) == 0) out += L',';
        out += d[i];
    }
    return out;
}

static std::wstring footerText() {
    if (g_building.load()) {
        if (g_buildPhase.load() == 2) return L"正在保存索引…";
        return L"正在索引 " + withCommas(g_buildCount.load()) + L" 个对象…";
    }
    auto ix = getIndex();
    if (!ix) return L"索引未就绪，正在后台建立…";

    std::wstring total = withCommas(ix->ents.size());
    bool idle = (GetWindowTextLengthW(g_hEdit) == 0);

    if (idle) return L"已索引 " + total + L" 个对象   ·   双击 Ctrl 唤出";
    if (g_res.empty()) return L"没有找到匹配项   ·   已索引 " + total + L" 项";
    return L"找到 " + withCommas(g_res.size()) + L" 项   ·   已索引 " + total + L" 项";
}

// ---------------------------------------------------------------- tray
static void addTray() {
    if (g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon  = g_appIcon ? g_appIcon : LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy(nid.szTip, L"SwiftFind — 双击 Ctrl 唤出");
    Shell_NotifyIconW(NIM_ADD, &nid);
    g_trayAdded = true;
}

static void removeTray() {
    if (!g_trayAdded) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = 1;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g_trayAdded = false;
}

static bool startupEnabled() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t val[MAX_PATH * 2];
    DWORD cb = sizeof(val), type = 0;
    bool on = RegQueryValueExW(k, APP_NAME, nullptr, &type, (LPBYTE)val, &cb) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

static void setStartup(bool on) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring q = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (const BYTE*)q.c_str(),
                       (DWORD)((q.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(k, APP_NAME);
    }
    RegCloseKey(k);
}

static DWORD WINAPI rebuildProc(void*) {
    buildIndexBlocking(true);
    if (g_hwnd) PostMessageW(g_hwnd, WM_REBUILT, 0, 0);
    return 0;
}

static void startRebuildThread() {
    if (g_building.exchange(true)) return;
    g_building.store(false);
    HANDLE h = CreateThread(nullptr, 0, rebuildProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);   // detached: the thread signals completion via WM_REBUILT
}

static void showSettings();

// One menu, two entry points: the tray icon and a right-click inside the
// search window. Keeping a single builder means hiding the tray icon never
// strands 设置 / 重建索引 / 退出.
static void showAppMenu(bool fromWindow) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    int n = 1, idOpen = 0, idSettings = 0, idReindex = 0, idQuit = 0;
    if (!fromWindow) {
        idOpen = n++;
        AppendMenuW(m, MF_STRING, idOpen, L"打开搜索框  (双击 Ctrl)");
    }
    idSettings = n++; AppendMenuW(m, MF_STRING, idSettings, L"设置…  (Ctrl+,)");
    idReindex  = n++; AppendMenuW(m, MF_STRING, idReindex,  L"重建索引");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    idQuit     = n++; AppendMenuW(m, MF_STRING, idQuit,     L"退出");

    SetForegroundWindow(g_hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);   // let the menu close cleanly

    if (!cmd) return;
    if (cmd == idOpen)          showWindow();
    else if (cmd == idSettings) showSettings();
    else if (cmd == idReindex)  startRebuildThread();
    else if (cmd == idQuit)     DestroyWindow(g_hwnd);
}

static void showTrayMenu() { showAppMenu(false); }

// ---------------------------------------------------------------- settings panel
// Opens only from the tray menu. Custom-drawn to match the launcher.

static bool regGetBool(const wchar_t* name, bool def) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SwiftFind", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return def;
    DWORD v = 0, cb = sizeof(v), type = 0;
    bool ok = (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&v, &cb) == ERROR_SUCCESS);
    RegCloseKey(k);
    return ok ? (v != 0) : def;
}

static void regSetBool(const wchar_t* name, bool val) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SwiftFind", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    DWORD v = val ? 1 : 0;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

static int regGetInt(const wchar_t* name, int def) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\SwiftFind", 0, KEY_READ, &k) != ERROR_SUCCESS)
        return def;
    DWORD v = 0, cb = sizeof(v), type = 0;
    bool ok = (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&v, &cb) == ERROR_SUCCESS);
    RegCloseKey(k);
    return ok ? (int)v : def;
}

static void regSetInt(const wchar_t* name, int val) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\SwiftFind", 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    DWORD v = (DWORD)val;
    RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(k);
}

// Recompute the palette and repaint everything that draws with it.
static void refreshThemeWindows() {
    applyTheme();

    if (g_hList)
        SetWindowTheme(g_hList, g_dark ? L"DarkMode_Explorer" : L"", nullptr);

    if (g_hwnd) {
        InvalidateRect(g_hwnd, nullptr, TRUE);
        if (g_hEdit) InvalidateRect(g_hEdit, nullptr, TRUE);
        if (g_hList) InvalidateRect(g_hList, nullptr, TRUE);
    }
    if (g_hSettings) InvalidateRect(g_hSettings, nullptr, TRUE);
}

struct SettingsRects {
    RECT titleBar;
    RECT btnClose;
    RECT rowAutoStart;
    RECT rowHideOnBlur;
    RECT rowFollowMouse;
    RECT rowTray;
    RECT rowTheme;
    RECT segTheme;
    RECT btnReindex;
    RECT btnOpenDir;
    int  dividerY;
    int  sectionY;
    int  statsY;
    int  aboutY;
};
static SettingsRects g_sr;

static int settingsW() { return S(480); }
static int settingsH() { return S(484); }

static void layoutSettings(int w, int h) {
    (void)h;
    int pad   = S(24);
    int rowH  = S(56);

    g_sr.titleBar  = { 0, 0, w, S(52) };
    g_sr.btnClose  = { w - S(46), S(10), w - S(14), S(42) };

    int y = S(56);
    g_sr.rowAutoStart   = { 0, y, w, y + rowH };  y += rowH;
    g_sr.rowHideOnBlur  = { 0, y, w, y + rowH };  y += rowH;
    g_sr.rowFollowMouse = { 0, y, w, y + rowH };  y += rowH;
    g_sr.rowTray        = { 0, y, w, y + rowH };  y += rowH;
    g_sr.rowTheme       = { 0, y, w, y + rowH };  y += rowH;

    // segmented control: 3 equal cells, right-aligned in its row
    int segW = S(216), segH = S(30);
    int segTop = g_sr.rowTheme.top + ((g_sr.rowTheme.bottom - g_sr.rowTheme.top) - segH) / 2;
    g_sr.segTheme = { w - pad - segW, segTop, w - pad, segTop + segH };

    g_sr.dividerY = S(340);
    g_sr.sectionY = S(350);
    g_sr.statsY   = S(372);
    g_sr.aboutY   = S(446);

    int by = S(400), bh = S(34);
    g_sr.btnReindex = { pad, by, pad + S(104), by + bh };
    g_sr.btnOpenDir = { pad + S(112), by, pad + S(112) + S(124), by + bh };
}

// ---------------------------------------------------------------- anti-aliasing
// GDI's RoundRect/Ellipse are hard-edged: at 24px tall the pill and knob show
// obvious stair-stepping. GDI+ fills the same geometry with a coverage mask, so
// the curves come out smooth. Only the shapes need it - text stays GDI.
static ULONG_PTR g_gdiplusToken = 0;

static Gdiplus::Color toGp(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

static void aaPath(Gdiplus::GraphicsPath& p, int x, int y, int w, int h, int rad) {
    int d = rad * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d <= 0) { p.AddRectangle(Gdiplus::Rect(x, y, w, h)); return; }
    p.AddArc(x, y, d, d, 180.0f, 90.0f);
    p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

// Fill a rounded rect, optionally with a 1px border drawn as a slightly larger
// path underneath - that keeps the outline anti-aliased too, which a GDI pen
// on a GDI+ fill would not be.
static void aaRoundRect(Gdiplus::Graphics& g, const RECT& r, int rad,
                        COLORREF fill, COLORREF border) {
    int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    Gdiplus::GraphicsPath outer;
    aaPath(outer, r.left, r.top, w, h, rad);
    Gdiplus::SolidBrush bo(toGp(border));
    g.FillPath(&bo, &outer);
    if (fill != border && w > 2 && h > 2) {
        Gdiplus::GraphicsPath inner;
        aaPath(inner, r.left + 1, r.top + 1, w - 2, h - 2, rad > 1 ? rad - 1 : 0);
        Gdiplus::SolidBrush bf(toGp(fill));
        g.FillPath(&bf, &inner);
    }
}

static void aaEllipse(Gdiplus::Graphics& g, int cx, int cy, int d, COLORREF col) {
    Gdiplus::SolidBrush b(toGp(col));
    g.FillEllipse(&b, cx - d / 2, cy - d / 2, d, d);
}

static void aaBegin(Gdiplus::Graphics& g) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

static void drawToggle(HDC dc, const RECT& row, bool on) {
    int tw = S(44), th = S(24);
    RECT tr;
    tr.left   = row.right - S(24) - tw;
    tr.right  = row.right - S(24);
    tr.top    = row.top + ((row.bottom - row.top) - th) / 2;
    tr.bottom = tr.top + th;

    Gdiplus::Graphics g(dc);
    aaBegin(g);
    aaRoundRect(g, tr, th / 2, on ? g_theme.accent : g_theme.toggleOff,
                on ? g_theme.accent : g_theme.toggleOffBorder);

    int d  = th - S(6);
    int cy = (tr.top + tr.bottom) / 2;
    int cx = on ? (tr.right - S(3) - d / 2) : (tr.left + S(3) + d / 2);
    aaEllipse(g, cx, cy, d, g_theme.knob);
}

static void drawRowToggle(HDC dc, const RECT& row, const wchar_t* label, bool on) {
    HGDIOBJ of = SelectObject(dc, g_fName);
    SetTextColor(dc, g_theme.text);
    RECT lr = { row.left + S(24), row.top, row.right - S(96), row.bottom };
    DrawTextW(dc, label, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, of);
    drawToggle(dc, row, on);
}

static void drawButton(HDC dc, const RECT& r, const wchar_t* label) {
    HBRUSH b = CreateSolidBrush(g_theme.btnBg);
    HPEN   p = CreatePen(PS_SOLID, 1, g_theme.btnBorder);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, S(8), S(8));
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);

    HGDIOBJ of = SelectObject(dc, g_fName);
    SetTextColor(dc, g_theme.text);
    RECT tr = r;
    DrawTextW(dc, label, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(dc, of);
}

// 3-cell segmented control: 0 = light, 1 = dark, 2 = follow system
static void drawSegmented(HDC dc, const RECT& r, int active) {
    Gdiplus::Graphics g(dc);
    aaBegin(g);
    aaRoundRect(g, r, S(9), g_theme.segBg, g_theme.segBorder);

    static const wchar_t* labels[3] = { L"浅色", L"深色", L"跟随系统" };
    int w = r.right - r.left;
    HGDIOBJ of = SelectObject(dc, g_fName);

    for (int i = 0; i < 3; ++i) {
        int l = r.left + w * i / 3;
        int rr = r.left + w * (i + 1) / 3;
        RECT cell = { l + S(2), r.top + S(2), rr - S(2), r.bottom - S(2) };

        if (i == active) {
            aaRoundRect(g, cell, S(8), g_theme.accent, g_theme.accent);
            SetTextColor(dc, RGB(255, 255, 255));
        } else {
            SetTextColor(dc, g_theme.textDim);
        }
        DrawTextW(dc, labels[i], -1, &cell, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    SelectObject(dc, of);
}

static void hLine(HDC dc, int x1, int x2, int y) {
    HPEN p = CreatePen(PS_SOLID, 1, g_theme.sep);
    HGDIOBJ op = SelectObject(dc, p);
    MoveToEx(dc, x1, y, nullptr);
    LineTo(dc, x2, y);
    SelectObject(dc, op);
    DeleteObject(p);
}

static std::wstring indexSummary() {
    auto ix = getIndex();
    if (!ix) return L"索引尚未建立";

    uint64_t sz = 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(g_indexPath.c_str(), GetFileExInfoStandard, &fad))
        sz = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;

    // "%.1f MB" without printf: work in tenths of a MB.
    uint64_t tenths = (sz * 10 + 512ULL * 1024ULL) / (1024ULL * 1024ULL);
    std::wstring buf = utoW(tenths / 10) + L"." +
                       wchar_t(L'0' + (tenths % 10)) + L" MB";
    return withCommas(ix->ents.size()) + L" 个对象   ·   " + buf;
}

static LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, S(14), S(14));
            SetWindowRgn(hwnd, rgn, TRUE);
            layoutSettings(rc.right, rc.bottom);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            paintFrame(dc, rc);
            SetBkMode(dc, TRANSPARENT);

            // title
            HGDIOBJ of = SelectObject(dc, g_fTitle);
            SetTextColor(dc, g_theme.text);
            RECT tr = { S(24), S(12), rc.right - S(56), S(46) };
            DrawTextW(dc, L"设置", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            // close
            SelectObject(dc, g_fName);
            SetTextColor(dc, g_theme.textDim);
            RECT cr = g_sr.btnClose;
            DrawTextW(dc, L"\u00D7", -1, &cr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

            drawRowToggle(dc, g_sr.rowAutoStart, L"开机自动启动", startupEnabled());
            drawRowToggle(dc, g_sr.rowHideOnBlur, L"失焦自动隐藏", g_hideOnBlur);
            drawRowToggle(dc, g_sr.rowFollowMouse, L"跟随鼠标位置打开", g_followMouse);

            // tray row: when off, spell out where the menu went - otherwise the
            // only entry point to 设置 / 退出 has just been removed.
            {
                HGDIOBJ of2 = SelectObject(dc, g_fName);
                SetTextColor(dc, g_theme.text);
                RECT lr = { S(24), g_sr.rowTray.top, g_sr.rowTray.right - S(96), g_sr.rowTray.bottom };
                if (g_showTray) {
                    DrawTextW(dc, L"显示托盘图标", -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                } else {
                    lr.top += S(5);
                    DrawTextW(dc, L"显示托盘图标", -1, &lr, DT_LEFT | DT_SINGLELINE | DT_TOP);
                    SelectObject(dc, g_fPath);
                    SetTextColor(dc, g_theme.textDim);
                    RECT hr = { S(24), g_sr.rowTray.top + S(27),
                                g_sr.rowTray.right - S(96), g_sr.rowTray.bottom };
                    DrawTextW(dc, L"已隐藏 · 搜索框内右键或 Ctrl+, 打开设置", -1, &hr,
                              DT_LEFT | DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS);
                }
                SelectObject(dc, of2);
                drawToggle(dc, g_sr.rowTray, g_showTray);
            }

            // theme row: label + segmented control
            SelectObject(dc, g_fName);
            SetTextColor(dc, g_theme.text);
            RECT tl = { S(24), g_sr.rowTheme.top, g_sr.segTheme.left - S(12), g_sr.rowTheme.bottom };
            DrawTextW(dc, L"主题", -1, &tl, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            drawSegmented(dc, g_sr.segTheme, g_themeMode);

            hLine(dc, S(24), rc.right - S(24), g_sr.dividerY);

            // index section
            SelectObject(dc, g_fPath);
            SetTextColor(dc, g_theme.textDim);
            RECT sr = { S(24), g_sr.sectionY, rc.right - S(24), g_sr.sectionY + S(20) };
            DrawTextW(dc, L"索引", -1, &sr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            SelectObject(dc, g_fName);
            SetTextColor(dc, g_theme.text);
            std::wstring sum = indexSummary();
            RECT ir = { S(24), g_sr.statsY, rc.right - S(24), g_sr.statsY + S(22) };
            DrawTextW(dc, sum.c_str(), (int)sum.size(), &ir,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            drawButton(dc, g_sr.btnReindex, g_building.load() ? L"索引中…" : L"重建索引");
            drawButton(dc, g_sr.btnOpenDir, L"打开索引目录");

            // about
            SelectObject(dc, g_fPath);
            SetTextColor(dc, g_theme.textDim);
            RECT ar = { S(24), g_sr.aboutY, rc.right - S(24), g_sr.aboutY + S(20) };
            DrawTextW(dc, L"SwiftFind 1.0.0   ·   双击 Ctrl 唤出搜索框", -1, &ar,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER);

            SelectObject(dc, of);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_NCHITTEST: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &p);
            if (PtInRect(&g_sr.titleBar, p) && !PtInRect(&g_sr.btnClose, p))
                return HTCAPTION;
            break;
        }

        case WM_LBUTTONUP: {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&g_sr.btnClose, p)) { DestroyWindow(hwnd); return 0; }

            if (PtInRect(&g_sr.rowAutoStart, p)) {
                setStartup(!startupEnabled());
                InvalidateRect(hwnd, &g_sr.rowAutoStart, FALSE);
                return 0;
            }
            if (PtInRect(&g_sr.rowHideOnBlur, p)) {
                g_hideOnBlur = !g_hideOnBlur;
                regSetBool(L"HideOnBlur", g_hideOnBlur);
                InvalidateRect(hwnd, &g_sr.rowHideOnBlur, FALSE);
                return 0;
            }
            if (PtInRect(&g_sr.rowFollowMouse, p)) {
                g_followMouse = !g_followMouse;
                regSetBool(L"FollowMouse", g_followMouse);
                InvalidateRect(hwnd, &g_sr.rowFollowMouse, FALSE);
                return 0;
            }
            if (PtInRect(&g_sr.rowTray, p)) {
                g_showTray = !g_showTray;
                regSetBool(L"ShowTrayIcon", g_showTray);
                if (g_showTray) addTray(); else removeTray();
                InvalidateRect(hwnd, &g_sr.rowTray, FALSE);
                return 0;
            }
            if (PtInRect(&g_sr.rowTheme, p)) {
                int segW = (g_sr.segTheme.right - g_sr.segTheme.left) / 3;
                int idx  = (segW > 0) ? (p.x - g_sr.segTheme.left) / segW : 0;
                if (idx < 0) idx = 0;
                if (idx > 2) idx = 2;
                g_themeMode = idx;
                regSetInt(L"Theme", idx);
                refreshThemeWindows();
                return 0;
            }
            if (PtInRect(&g_sr.btnReindex, p)) {
                startRebuildThread();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtInRect(&g_sr.btnOpenDir, p)) {
                std::wstring dir = dataDir();
                ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            return 0;
        }

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) { DestroyWindow(hwnd); return 0; }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            g_hSettings = nullptr;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void showSettings() {
    if (g_hSettings && IsWindow(g_hSettings)) {
        SetForegroundWindow(g_hSettings);
        InvalidateRect(g_hSettings, nullptr, FALSE);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_DROPSHADOW;
        wc.lpfnWndProc   = SettingsProc;
        wc.hInstance     = g_hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"SwiftFindSettings";
        wc.hIcon         = g_appIcon;
        wc.hIconSm       = g_appIcon;
        RegisterClassExW(&wc);
        registered = true;
    }

    int w = settingsW(), h = settingsH();

    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);

    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;

    g_hSettings = CreateWindowExW(0, L"SwiftFindSettings", L"设置", WS_POPUP,
                                  x, y, w, h, nullptr, nullptr, g_hInst, nullptr);
    if (!g_hSettings) return;

    ShowWindow(g_hSettings, SW_SHOW);
    SetForegroundWindow(g_hSettings);
}

// ---------------------------------------------------------------- edit subclass
static WNDPROC g_editProc = nullptr;

// Two-state window: collapsed (input only) and expanded (with results).
static void setWindowHeight(int h) {
    if (!g_hwnd) return;
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    if ((rc.bottom - rc.top) == h) return;
    SetWindowPos(g_hwnd, nullptr, rc.left, rc.top,
                 rc.right - rc.left, h, SWP_NOZORDER | SWP_NOACTIVATE);

    // growing near the bottom edge would push the list off-screen; slide up
    HMONITOR mon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        GetWindowRect(g_hwnd, &rc);
        int ny = rc.top;
        if (rc.bottom > mi.rcWork.bottom) ny = mi.rcWork.bottom - h;
        if (ny < mi.rcWork.top) ny = mi.rcWork.top;
        if (ny != rc.top)
            SetWindowPos(g_hwnd, nullptr, rc.left, ny, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

static void refreshResults() {
    wchar_t buf[512];
    GetWindowTextW(g_hEdit, buf, 512);

    auto ix = getIndex();
    if (ix) searchIndex(*ix, buf, g_res);
    else g_res.clear();

    SendMessageW(g_hList, LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < g_res.size(); ++i)
        SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)L"");
    if (!g_res.empty()) SendMessageW(g_hList, LB_SETCURSEL, 0, 0);

    bool emptyQuery = (buf[0] == 0);

    // list only when there is something to list; otherwise the parent paints
    // the "no match" hint itself
    ShowWindow(g_hList, g_res.empty() ? SW_HIDE : SW_SHOW);
    setWindowHeight(emptyQuery ? S(64) : S(520));

    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void launchSelected() {
    int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
    if (sel < 0 || sel >= (int)g_res.size()) return;
    auto ix = getIndex();
    if (!ix) return;
    std::wstring p = ix->fullPath(g_res[sel].idx);
    hideWindow();
    launchPath(p);
}

static LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN: {
            int sel = (int)SendMessageW(g_hList, LB_GETCURSEL, 0, 0);
            int cnt = (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
            if (wp == VK_DOWN) {
                if (cnt > 0) {
                    sel = (sel < 0) ? 0 : std::min(sel + 1, cnt - 1);
                    SendMessageW(g_hList, LB_SETCURSEL, sel, 0);
                }
                return 0;
            }
            if (wp == VK_UP) {
                if (cnt > 0) {
                    sel = (sel <= 0) ? 0 : sel - 1;
                    SendMessageW(g_hList, LB_SETCURSEL, sel, 0);
                }
                return 0;
            }
            if (wp == VK_RETURN) { launchSelected(); return 0; }
            if (wp == VK_ESCAPE) { hideWindow(); return 0; }
            if (wp == VK_OEM_COMMA && (GetKeyState(VK_CONTROL) & 0x8000)) {
                showSettings();
                return 0;
            }
            break;
        }
        case WM_RBUTTONUP:
            showAppMenu(true);
            return 0;
    }
    return CallWindowProcW(g_editProc, hwnd, msg, wp, lp);
}

// Right-click on the results list must reach the app menu too - an owner-drawn
// LISTBOX swallows it otherwise, so the menu would only work on the text field.
static WNDPROC g_listProc = nullptr;
static LRESULT CALLBACK ListProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_RBUTTONUP) { showAppMenu(true); return 0; }
    return CallWindowProcW(g_listProc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- layout
static void layoutChildren(int w, int h) {
    int headerH = S(58);
    int footerH = S(26);
    int padX    = S(16);

    MoveWindow(g_hEdit, padX, S(12), w - padX * 2, headerH - S(18), TRUE);

    int listH = h - headerH - footerH - 1;
    if (listH < 0) listH = 0;      // collapsed state: list has no area
    MoveWindow(g_hList, 0, headerH + 1, w, listH, TRUE);

    SendMessageW(g_hList, LB_SETITEMHEIGHT, 0, S(48));
}

// ---------------------------------------------------------------- window proc
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hEdit = CreateWindowExW(0, L"EDIT", L"",
                                      WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                                      0, 0, 10, 10, hwnd, (HMENU)ID_EDIT, g_hInst, nullptr);
            g_hList = CreateWindowExW(0, L"LISTBOX", L"",
                                      WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOTIFY |
                                      LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                      0, 0, 10, 10, hwnd, (HMENU)ID_LIST, g_hInst, nullptr);

            SendMessageW(g_hEdit, WM_SETFONT, (WPARAM)g_fEdit, TRUE);
            SendMessageW(g_hList, WM_SETFONT, (WPARAM)g_fName, TRUE);
            SendMessageW(g_hEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"搜索应用和文件…");

            g_editProc = (WNDPROC)SetWindowLongPtrW(g_hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
            g_listProc = (WNDPROC)SetWindowLongPtrW(g_hList, GWLP_WNDPROC, (LONG_PTR)ListProc);
            return 0;
        }

        case WM_SIZE: {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            layoutChildren(cw, ch);
            HRGN rgn = CreateRoundRectRgn(0, 0, cw + 1, ch + 1, S(14), S(14));
            SetWindowRgn(hwnd, rgn, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            paintFrame(dc, rc);

            bool compact = (GetWindowTextLengthW(g_hEdit) == 0);
            if (!compact) {
                // no-match hint (the list stays hidden so the parent can draw here)
                if (!IsWindowVisible(g_hList)) {
                    RECT ar = { 0, S(58), rc.right, rc.bottom - S(26) };
                    const wchar_t* hint = L"没有找到匹配项";
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, g_theme.textDim);
                    HGDIOBJ oh = SelectObject(dc, g_fName);
                    DrawTextW(dc, hint, -1, &ar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(dc, oh);
                }

                // footer
                int footerH = S(26);
                RECT fr = { S(16), rc.bottom - footerH, rc.right - S(16), rc.bottom - S(6) };
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, g_theme.textDim);
                HGDIOBJ of = SelectObject(dc, g_fPath);
                std::wstring ft = footerText();
                DrawTextW(dc, ft.c_str(), (int)ft.size(), &fr,
                          DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                SelectObject(dc, of);

                // separator above footer
                HPEN p = CreatePen(PS_SOLID, 1, g_theme.sep);
                HGDIOBJ op = SelectObject(dc, p);
                MoveToEx(dc, 0, rc.bottom - footerH - 1, nullptr);
                LineTo(dc, rc.right, rc.bottom - footerH - 1);
                SelectObject(dc, op);
                DeleteObject(p);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, g_theme.bg);
            SetTextColor(dc, g_theme.text);
            return (LRESULT)g_brBg;
        }

        case WM_CTLCOLORLISTBOX: {
            HDC dc = (HDC)wp;
            SetBkColor(dc, g_theme.bg);
            SetTextColor(dc, g_theme.text);
            return (LRESULT)g_brBg;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT d = (LPDRAWITEMSTRUCT)lp;
            if (d->CtlID != ID_LIST || d->itemID == (UINT)-1) break;
            if (d->itemID >= g_res.size()) break;

            auto ix = getIndex();
            if (!ix) break;

            const Result& r = g_res[d->itemID];
            const Entry& e  = ix->ents[r.idx];

            bool sel = (d->itemState & ODS_SELECTED) != 0;
            HBRUSH bg = CreateSolidBrush(sel ? g_theme.selBg : g_theme.bg);
            FillRect(d->hDC, &d->rcItem, bg);
            DeleteObject(bg);

            SetBkMode(d->hDC, TRANSPARENT);

            // icon
            std::wstring full = ix->fullPath(r.idx);
            HICON ic = g_icons.get(full);
            int isz = S(24);
            int iy  = d->rcItem.top + ((d->rcItem.bottom - d->rcItem.top) - isz) / 2;
            DrawIconEx(d->hDC, d->rcItem.left + S(16), iy, ic, isz, isz, 0, nullptr, DI_NORMAL);

            // name
            std::wstring name = toWide(ix->nameOf(e));
            if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".lnk") == 0)
                name.resize(name.size() - 4);

            int tx = d->rcItem.left + S(16) + isz + S(12);
            int tw = d->rcItem.right - tx - S(12);

            RECT rn = { tx, d->rcItem.top + S(6), tx + tw, d->rcItem.top + S(6) + S(20) };
            SetTextColor(d->hDC, g_theme.text);
            HGDIOBJ of = SelectObject(d->hDC, g_fName);
            DrawTextW(d->hDC, name.c_str(), (int)name.size(), &rn,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            // subtitle
            std::wstring sub;
            if (e.kind == 2) sub = L"应用程序";
            else sub = toWide(ix->dirOf(e));

            RECT rp = { tx, d->rcItem.top + S(25), tx + tw, d->rcItem.top + S(25) + S(17) };
            SetTextColor(d->hDC, g_theme.textDim);
            SelectObject(d->hDC, g_fPath);
            DrawTextW(d->hDC, sub.c_str(), (int)sub.size(), &rp,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
            SelectObject(d->hDC, of);

            return TRUE;
        }

        case WM_COMMAND: {
            int id   = LOWORD(wp);
            int code = HIWORD(wp);
            if (id == ID_EDIT && code == EN_CHANGE) {
                refreshResults();
                return 0;
            }
            if (id == ID_LIST && code == LBN_DBLCLK) {
                launchSelected();
                return 0;
            }
            if (id == ID_LIST && code == LBN_SELCHANGE) {
                SetFocus(g_hEdit);      // keep typing working after a click
                return 0;
            }
            break;
        }

        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && g_visible && !g_noHide && g_hideOnBlur) {
                // delay slightly so clicking a result still works
                SetTimer(hwnd, 7, 120, nullptr);
            }
            return 0;

        case WM_TIMER:
            if (wp == 7) {
                KillTimer(hwnd, 7);
                HWND fg = GetForegroundWindow();
                if (fg != hwnd && !IsChild(hwnd, fg)) hideWindow();
            } else if (wp == 9) {
                if (!g_building.load()) KillTimer(hwnd, 9);
                RECT rc;
                GetClientRect(hwnd, &rc);
                rc.top = rc.bottom - S(26);
                InvalidateRect(hwnd, &rc, FALSE);
            }
            return 0;

        case WM_SETTINGCHANGE:
            if (lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0 && g_themeMode == 2)
                refreshThemeWindows();
            return 0;

        case WM_TOGGLE:
            toggleWindow();
            return 0;

        case WM_REBUILT:
            if (g_visible) refreshResults();
            return 0;

        case WM_TRAY:
            if (lp == WM_LBUTTONUP) showWindow();
            else if (lp == WM_RBUTTONUP) showTrayMenu();
            return 0;

        case WM_CLOSE:
            hideWindow();
            return 0;

        case WM_DESTROY:
            removeTray();
            if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- keyboard hook
static LRESULT CALLBACK LLKeyboard(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && g_hwnd) {
        bool down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
        if (down) {
            KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lp;
            if (k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL) {
                DWORD now = GetTickCount();
                if (g_lastCtrl && (now - g_lastCtrl) < 450) {
                    g_lastCtrl = 0;
                    PostMessageW(g_hwnd, WM_TOGGLE, 0, 0);
                } else {
                    g_lastCtrl = now;
                }
            } else {
                g_lastCtrl = 0;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// ---------------------------------------------------------------- init helpers
static std::wstring dataDir() {
    wchar_t buf[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf)))
        return L".";
    std::wstring d = std::wstring(buf) + L"\\SwiftFind";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static void createFonts() {
    int hEdit = -MulDiv(19, g_dpi, 72);
    int hName = -MulDiv(11, g_dpi, 72);
    int hPath = -MulDiv(9,  g_dpi, 72);

    g_fEdit = CreateFontW(hEdit, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fName = CreateFontW(hName, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_fPath = CreateFontW(hPath, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                          OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    int hTitle = -MulDiv(15, g_dpi, 72);
    g_fTitle = CreateFontW(hTitle, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET,
                           OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

// ---------------------------------------------------------------- CLI modes
static void attachConsole() {
    // if stdout is already redirected to a file or pipe, keep it
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD t = GetFileType(h);
        if (t == FILE_TYPE_DISK || t == FILE_TYPE_PIPE) return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
}

static int cliSearch(const std::wstring& q) {
    auto ix = getIndex();
    if (!ix) {
        printf("index missing, building...\n");
        buildIndexBlocking(true);
        ix = getIndex();
    }
    if (!ix) return 1;

    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);

    std::vector<Result> out;
    QueryPerformanceCounter(&t0);
    searchIndex(*ix, q, out);
    QueryPerformanceCounter(&t1);

    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;

    printf("query: %s\n", toUtf8(q).c_str());
    printf("index: %u entries, %u dirs, arena %zu + %zu bytes\n",
           (unsigned)ix->ents.size(), (unsigned)ix->dirOff.size(),
           ix->dirArena.size(), ix->nameArena.size());
    printf("search: %.1f ms, %zu hits\n", ms, out.size());
    for (size_t i = 0; i < out.size() && i < 12; ++i) {
        std::wstring p = ix->fullPath(out[i].idx);
        printf("  [%4d] %s\n", out[i].score, toUtf8(p).c_str());
    }
    return 0;
}

static int cliReindex() {
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    buildIndexBlocking(true);
    QueryPerformanceCounter(&t1);

    auto ix = getIndex();
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart;
    printf("built in %.1f s\n", ms / 1000.0);
    if (ix) {
        size_t apps = 0, dirs = 0;
        for (auto& e : ix->ents) { if (e.kind == 2) apps++; else if (e.kind == 1) dirs++; }
        printf("entries: %u (apps %zu, dirs %zu, files %zu)\n",
               (unsigned)ix->ents.size(), apps, dirs, ix->ents.size() - apps - dirs);
        printf("dirs: %u, dirArena %zu B, nameArena %zu B\n",
               (unsigned)ix->dirOff.size(), ix->dirArena.size(), ix->nameArena.size());
        printf("index file: %s\n", toUtf8(g_indexPath).c_str());
    }
    return 0;
}

// ---------------------------------------------------------------- entry
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    g_hInst = hInst;

    // Needed before the first paint: the settings panel uses GDI+ to get
    // anti-aliased toggles. Cheap - gdiplus.dll is a system component.
    Gdiplus::GdiplusStartupInput gdiInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiInput, nullptr);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    bool showAtStart = false;
    bool settingsAtStart = false;
    std::wstring searchQ;
    bool reindex = false;

    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--show") showAtStart = true;
        else if (a == L"--settings") settingsAtStart = true;
        else if (a == L"--nohide") g_noHide = true;
        else if (a == L"--reindex") reindex = true;
        else if (a == L"--query" && i + 1 < argc) { g_startQuery = argv[++i]; showAtStart = true; }
        else if (a == L"--search" && i + 1 < argc) searchQ = argv[++i];
    }
    if (argv) LocalFree(argv);

    // DPI
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL (WINAPI *SetDpiCtx)(void*);
        auto fn = (SetDpiCtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (fn) fn((void*)-4);  // PER_MONITOR_AWARE_V2
    }
    {
        HDC dc = GetDC(nullptr);
        g_dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (g_dpi <= 0) g_dpi = 96;
    }

    g_indexPath = dataDir() + L"\\index.bin";
    g_hideOnBlur = regGetBool(L"HideOnBlur", true);
    g_followMouse = regGetBool(L"FollowMouse", false);
    g_showTray = regGetBool(L"ShowTrayIcon", true);

    g_themeMode = regGetInt(L"Theme", 2);
    if (g_themeMode < 0 || g_themeMode > 2) g_themeMode = 2;
    applyTheme();

    // CLI: reindex
    if (reindex) {
        attachConsole();
        int rc = cliReindex();
        return rc;
    }

    // CLI: search (needs index in memory)
    if (!searchQ.empty()) {
        attachConsole();
        auto ix = loadIndex();
        if (ix) setIndex(ix);
        int rc = cliSearch(searchQ);
        return rc;
    }

    // ---- GUI mode ----
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"SwiftFind_SingleInstance");
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND w = FindWindowW(L"SwiftFindWnd", nullptr);
        if (w) PostMessageW(w, WM_TOGGLE, 0, 0);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    g_appIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    createFonts();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SwiftFindWnd";
    wc.hIcon         = g_appIcon;
    wc.hIconSm       = g_appIcon;
    RegisterClassExW(&wc);

    int w = S(760), h = S(520);
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"SwiftFindWnd", L"SwiftFind",
                             WS_POPUP, 100, 100, w, h,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // rounded corners on Win11
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *SetAttr)(HWND, DWORD, LPCVOID, DWORD);
            auto fn = (SetAttr)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (fn) {
                int pref = 2;  // DWMWCP_ROUND
                fn(g_hwnd, 33, &pref, sizeof(pref));
            }
        }
    }

    layoutChildren(w, h);
    if (g_showTray) addTray();
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LLKeyboard, hInst, 0);
    SetTimer(g_hwnd, 9, 400, nullptr);

    // load index, build if absent
    auto ix = loadIndex();
    if (ix) setIndex(ix);
    else startRebuildThread();

    if (showAtStart) showWindow();
    if (settingsAtStart) showSettings();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN) {}
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
