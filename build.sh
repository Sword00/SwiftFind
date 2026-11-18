#!/usr/bin/env bash
# Build SwiftFind. Requires w64devkit (portable MinGW-w64).
# Override the toolchain location with W64DEVKIT=/path/to/w64devkit
set -e

TOOLCHAIN="${W64DEVKIT:-/c/Users/Z/w64devkit/w64devkit}"
export PATH="$TOOLCHAIN/bin:$PATH"

cd "$(dirname "$0")/src"

command -v g++ >/dev/null || { echo "g++ not found. Set W64DEVKIT."; exit 1; }

windres -I. res.rc -O coff -o res.o

# -Os beats -O2 by ~25 KB here; -ffunction-sections/--gc-sections then drop
# another ~4 KB of unreferenced libstdc++ members.
# Deliberately NOT enabled: -fno-exceptions (would strip .pdata/.xdata for our
# frames, turning an allocation failure into a crash instead of a clean exit).
g++ -Os -s -municode -mwindows -std=c++17 -DNDEBUG \
    -ffunction-sections -fdata-sections \
    main.cpp res.o -o SwiftFind.exe \
    -Wl,--gc-sections \
    -static -static-libgcc -static-libstdc++ \
    -lcomctl32 -lshell32 -lole32 -ldwmapi -lshlwapi -luxtheme -luuid \
    -lgdiplus \
    -luser32 -lgdi32 -ladvapi32

ls -l SwiftFind.exe | awk '{print "built src/SwiftFind.exe  ("$5" bytes)"}'
