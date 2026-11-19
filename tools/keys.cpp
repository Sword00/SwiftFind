// Sends two quick Ctrl taps so we can verify SwiftFind's global hotkey.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc >= 4 && wcscmp(argv[1], L"--move") == 0) {
        int x = _wtoi(argv[2]), y = _wtoi(argv[3]);
        SetCursorPos(x, y);
        wprintf(L"cursor -> %d,%d\n", x, y);
        return 0;
    }

    if (argc >= 4 && (wcscmp(argv[1], L"--click") == 0 || wcscmp(argv[1], L"--rclick") == 0)) {
        bool right = (wcscmp(argv[1], L"--rclick") == 0);
        int x = _wtoi(argv[2]), y = _wtoi(argv[3]);
        Sleep(300);
        SetCursorPos(x, y);
        Sleep(120);
        INPUT in[2] = {};
        in[0].type = INPUT_MOUSE;
        in[0].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        in[1].type = INPUT_MOUSE;
        in[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        SendInput(2, in, sizeof(INPUT));
        wprintf(L"%ls %d,%d\n", right ? L"right-clicked" : L"clicked", x, y);
        return 0;
    }

    if (argc >= 3 && wcscmp(argv[1], L"--wheel") == 0) {
        int notches = (argc >= 4) ? _wtoi(argv[3]) : 3;
        int dir = wcscmp(argv[2], L"up") == 0 ? 1 : -1;
        Sleep(200);
        for (int i = 0; i < notches; ++i) {
            INPUT in = {};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = (DWORD)(dir * WHEEL_DELTA);
            SendInput(1, &in, sizeof(INPUT));
            Sleep(30);
        }
        wprintf(L"wheel %ls x%d\n", dir > 0 ? L"up" : L"down", notches);
        return 0;
    }

    if (argc >= 6 && wcscmp(argv[1], L"--drag") == 0) {
        int x1 = _wtoi(argv[2]), y1 = _wtoi(argv[3]);
        int x2 = _wtoi(argv[4]), y2 = _wtoi(argv[5]);
        Sleep(250);
        SetCursorPos(x1, y1);
        Sleep(120);
        INPUT dn = {};
        dn.type = INPUT_MOUSE;
        dn.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &dn, sizeof(INPUT));
        for (int i = 1; i <= 12; ++i) {
            Sleep(25);
            SetCursorPos(x1 + (x2 - x1) * i / 12, y1 + (y2 - y1) * i / 12);
        }
        Sleep(120);
        INPUT up = {};
        up.type = INPUT_MOUSE;
        up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &up, sizeof(INPUT));
        wprintf(L"dragged %d,%d -> %d,%d\n", x1, y1, x2, y2);
        return 0;
    }

    if (argc >= 4 && wcscmp(argv[1], L"--repeat") == 0) {
        // Mimics keyboard auto-repeat: n key-downs with no key-up in between,
        // which is exactly what holding a key produces at the hook level.
        WORD vk = (WORD)wcstoul(argv[2], nullptr, 16);
        int n = _wtoi(argv[3]);
        int gap = (argc >= 5) ? _wtoi(argv[4]) : 30;
        Sleep(300);
        for (int i = 0; i < n; ++i) {
            INPUT in = {};
            in.type = INPUT_KEYBOARD;
            in.ki.wVk = vk;
            SendInput(1, &in, sizeof(INPUT));
            Sleep(gap);
        }
        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = vk;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
        wprintf(L"repeated vk 0x%02X x%d, gap %dms, then key-up\n", vk, n, gap);
        return 0;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--clear") == 0) {
        Sleep(300);
        INPUT in[4] = {};
        // Ctrl down
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LCONTROL;
        // 'A'
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = 'A';
        SendInput(2, in, sizeof(INPUT));
        INPUT up[2] = {};
        up[0].type = INPUT_KEYBOARD; up[0].ki.wVk = 'A'; up[0].ki.dwFlags = KEYEVENTF_KEYUP;
        up[1].type = INPUT_KEYBOARD; up[1].ki.wVk = VK_LCONTROL; up[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, up, sizeof(INPUT));
        Sleep(120);

        INPUT del[2] = {};
        del[0].type = INPUT_KEYBOARD; del[0].ki.wVk = VK_DELETE;
        del[1].type = INPUT_KEYBOARD; del[1].ki.wVk = VK_DELETE; del[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, del, sizeof(INPUT));
        wprintf(L"sent ctrl+a, delete\n");
        return 0;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--ctrlcomma") == 0) {
        Sleep(250);
        INPUT in[4] = {};
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = VK_LCONTROL;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = VK_OEM_COMMA;
        SendInput(2, in, sizeof(INPUT));
        INPUT up[2] = {};
        up[0].type = INPUT_KEYBOARD; up[0].ki.wVk = VK_OEM_COMMA; up[0].ki.dwFlags = KEYEVENTF_KEYUP;
        up[1].type = INPUT_KEYBOARD; up[1].ki.wVk = VK_LCONTROL; up[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, up, sizeof(INPUT));
        wprintf(L"sent ctrl+comma\n");
        return 0;
    }

    if (argc >= 3 && wcscmp(argv[1], L"--vk") == 0) {
        WORD vk = (WORD)wcstoul(argv[2], nullptr, 16);
        Sleep(200);
        INPUT in[2] = {};
        in[0].type = INPUT_KEYBOARD; in[0].ki.wVk = vk;
        in[1].type = INPUT_KEYBOARD; in[1].ki.wVk = vk; in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        wprintf(L"vk 0x%02X\n", vk);
        return 0;
    }

    if (argc >= 3 && wcscmp(argv[1], L"--type") == 0) {
        Sleep(250);
        // Unicode SendInput so CJK and punctuation behave like real typing.
        int n = (int)wcslen(argv[2]);
        for (int i = 0; i < n; ++i) {
            INPUT in[2] = {};
            in[0].type = INPUT_KEYBOARD;
            in[0].ki.wVk = 0;
            in[0].ki.wScan = argv[2][i];
            in[0].ki.dwFlags = KEYEVENTF_UNICODE;
            in[1] = in[0];
            in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            SendInput(2, in, sizeof(INPUT));
            Sleep(18);
        }
        wprintf(L"typed %d char(s)\n", n);
        return 0;
    }

    int gap = (argc >= 2) ? _wtoi(argv[1]) : 90;
    Sleep(400);

    for (int i = 0; i < 2; ++i) {
        INPUT in[2] = {};
        in[0].type = INPUT_KEYBOARD;
        in[0].ki.wVk = VK_LCONTROL;
        in[1].type = INPUT_KEYBOARD;
        in[1].ki.wVk = VK_LCONTROL;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(INPUT));
        Sleep(gap);
    }
    wprintf(L"sent double ctrl, gap=%dms\n", gap);
    return 0;
}
