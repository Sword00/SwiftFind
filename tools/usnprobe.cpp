// Probe: can a non-elevated process read the NTFS USN journal?
// Prints which open/FSCTL combination succeeds, so we know whether an
// incremental index update is possible without asking for admin.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <cstdio>

#ifndef FSCTL_QUERY_USN_JOURNAL
#define FSCTL_QUERY_USN_JOURNAL \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 61, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef FSCTL_READ_UNPRIVILEGED_USN_JOURNAL
#define FSCTL_READ_UNPRIVILEGED_USN_JOURNAL \
    CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 63, METHOD_NEITHER, FILE_ANY_ACCESS)
#endif

typedef struct {
    DWORDLONG UsnJournalID;
    DWORDLONG FirstUsn;
    DWORDLONG NextUsn;
    DWORDLONG LowestValidUsn;
    DWORDLONG MaxUsn;
    DWORDLONG MaximumSize;
    DWORDLONG AllocationDelta;
} JOURNAL_DATA;

typedef struct {
    DWORDLONG StartUsn;
    DWORD ReasonMask;
    DWORD ReturnOnlyOnClose;
    DWORDLONG Timeout;
    DWORDLONG BytesToWaitFor;
    DWORDLONG UsnJournalID;
} READ_DATA;

static bool elevated() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    TOKEN_ELEVATION el{};
    DWORD cb = 0;
    bool r = GetTokenInformation(t, TokenElevation, &el, sizeof(el), &cb) && el.TokenIsElevated;
    CloseHandle(t);
    return r;
}

static void tryOpen(const wchar_t* label, DWORD access) {
    HANDLE h = CreateFileW(L"\\\\.\\C:", access,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        wprintf(L"%-22ls open -> FAILED (err %lu)\n", label, GetLastError());
        return;
    }
    wprintf(L"%-22ls open -> OK\n", label);

    JOURNAL_DATA jd{};
    DWORD got = 0;
    if (DeviceIoControl(h, FSCTL_QUERY_USN_JOURNAL, nullptr, 0, &jd, sizeof(jd), &got, nullptr)) {
        wprintf(L"    QUERY_USN_JOURNAL  OK  id=%llu next=%llu\n",
                (unsigned long long)jd.UsnJournalID, (unsigned long long)jd.NextUsn);
    } else {
        wprintf(L"    QUERY_USN_JOURNAL  failed (err %lu)\n", GetLastError());
    }

    READ_DATA rd{};
    rd.StartUsn = 0;
    rd.ReasonMask = 0xFFFFFFFF;
    rd.UsnJournalID = jd.UsnJournalID;
    BYTE buf[4096];
    if (DeviceIoControl(h, FSCTL_READ_UNPRIVILEGED_USN_JOURNAL, &rd, sizeof(rd),
                        buf, sizeof(buf), &got, nullptr)) {
        DWORDLONG next = *(DWORDLONG*)buf;
        wprintf(L"    READ_UNPRIVILEGED  OK  bytes=%lu nextUsn=%llu\n",
                got, (unsigned long long)next);
    } else {
        wprintf(L"    READ_UNPRIVILEGED  failed (err %lu)\n", GetLastError());
    }
    CloseHandle(h);
}

int wmain() {
    wprintf(L"elevated = %ls\n\n", elevated() ? L"YES" : L"NO");
    tryOpen(L"GENERIC_READ", GENERIC_READ);
    tryOpen(L"FILE_READ_ATTRIBUTES", FILE_READ_ATTRIBUTES);
    tryOpen(L"access 0", 0);
    return 0;
}
